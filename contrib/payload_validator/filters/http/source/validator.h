#pragma once

#include <nlohmann/json-schema.hpp>
#include <string>

#include "envoy/server/filter_config.h"

using nlohmann::json;
using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

// For now only JSON payloads are validated.
class Validator {
public:
  virtual ~Validator() = default;

  virtual std::pair<bool, absl::optional<std::string>> validate(absl::string_view) PURE;
  virtual bool initialize(const std::string& schema) PURE;
};

// Base class for JSON validators: body, path and query params.
class JSONValidator : public Validator {
public:
  virtual ~JSONValidator() = default;

protected:
  bool initializeValidator(const std::string& schema);

  json_validator validator_;
};

// Param validator to validate query parameters and parameters in path's templates.
class ParamValidatorBase : public JSONValidator {
public:
  ParamValidatorBase() = delete;
  virtual ~ParamValidatorBase() = default;

  ParamValidatorBase(const std::string param_name) : param_name_(param_name) {}

  bool initialize(const std::string& schema) override;
  std::pair<bool, absl::optional<std::string>> validate(absl::string_view) override;


protected:
  std::string param_name_;
  std::string value_decorator_;
};

class QueryParamValidator : public ParamValidatorBase {
public:
    QueryParamValidator() = delete;
    virtual ~QueryParamValidator() = default;

  QueryParamValidator(const std::string param_name) : ParamValidatorBase(param_name), required_(true) {}

  bool required() const { return required_; }
  void required(bool required) { required_ = required; }
private:
  bool required_;
};



std::pair<bool, absl::optional<std::string>>
validateParams(const absl::flat_hash_map<std::string, std::unique_ptr<QueryParamValidator>>& params,
               const absl::string_view request);

class FixedPathSegmentValidator {
public:
    FixedPathSegmentValidator() = delete;
    FixedPathSegmentValidator(const absl::string_view segment, uint32_t number) : segment_(segment), segment_number_(number) {}    
    uint32_t segmentNumber() const {return segment_number_;}

    bool validate(const absl::string_view segment) const {return segment_ == Http::LowerCaseString(segment);}

private:
    Http::LowerCaseString segment_; 
    uint32_t segment_number_;  // Where in whole path the section is located.
};

class TemplatedPathSegmentValidator : public ParamValidatorBase {
public:
    TemplatedPathSegmentValidator() = delete;

    virtual ~TemplatedPathSegmentValidator() = default;
    TemplatedPathSegmentValidator(const std::string segment_name, const uint32_t number) : ParamValidatorBase(segment_name), segment_number_(number) {} 

    uint32_t segmentNumber() const {return segment_number_;}
   
private:
    uint32_t segment_number_;
};

struct PathTemplateValidator {
    std::vector<FixedPathSegmentValidator> fixed_segments_;
    // TODO: does it have to be a pointer?
    std::vector<std::unique_ptr<TemplatedPathSegmentValidator>> templated_segments_;
};

using AllowedPaths = absl::flat_hash_map<uint32_t, std::vector<std::unique_ptr<PathTemplateValidator>>>;

enum class PathValidationResult {
    // Given path matched the template.
    MATCHED,
    // Given path was matched, but validating path parameter failed.
    // For example if template is /user/{id} and id should be integer
    // path /user/joe matches but fails parameters validation.
    MATCHED_WITH_ERRORS,
    // None of configured templates matched the given path.
    NOT_MATCHED
};

std::pair<PathValidationResult, absl::optional<std::string>>
checkPath(const PathTemplateValidator& path_template, const std::vector<absl::string_view>& path_segments);

class JSONBodyValidator : public JSONValidator {
public:
    bool initialize(const std::string& schema) override;
    std::pair<bool, absl::optional<std::string>> validate(absl::string_view) override;
  bool active() const { return active_; }
private:
  // Validator has been initialized with schema definition.
  bool active_{false};
};

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
