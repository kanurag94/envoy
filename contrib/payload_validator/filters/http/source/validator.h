#pragma once

#include <nlohmann/json-schema.hpp>
#include <string>

#include "envoy/server/filter_config.h"

//#include "envoy/stats/scope.h"
//#include "envoy/stats/stats.h"
//##include "envoy/stats/stats_macros.h"

//#include "source/extensions/filters/http/common/factory_base.h"

//#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.h"
//#include
//"contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"

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

  // virtual std::pair<bool, absl::optional<std::string>> validate(const Buffer::Instance&) PURE;
  virtual std::pair<bool, absl::optional<std::string>> validate(absl::string_view) PURE;
  virtual bool initialize(const std::string& schema) PURE;
};

class JSONValidator : public Validator {
public:
  virtual ~JSONValidator() = default;

protected:
  bool initializeValidator(const std::string& schema);

  json_validator validator_;
};

class ParamValidator : public JSONValidator {
public:
  ParamValidator() = delete;
  virtual ~ParamValidator() = default;

  ParamValidator(const std::string param_name) : param_name_(param_name), required_(true) {}

  bool initialize(const std::string& schema) override;
  std::pair<bool, absl::optional<std::string>> validate(absl::string_view) override;

  bool required() const { return required_; }
  void required(bool required) { required_ = required; }

private:
  std::string param_name_;
  bool required_;
};

std::pair<bool, absl::optional<std::string>>
validateParams(const absl::flat_hash_map<std::string, std::unique_ptr<ParamValidator>>& params,
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

class TemplatedPathSegmentValidator : public JSONValidator {
public:
    TemplatedPathSegmentValidator() = delete;
    TemplatedPathSegmentValidator(const absl::string_view segment_name, const uint32_t number) : segment_name_(segment_name), segment_number_(number) {} 

    bool initialize(const std::string& schema) override;
    std::pair<bool, absl::optional<std::string>> validate(absl::string_view) override;
    uint32_t segmentNumber() const {return segment_number_;}
   
private:
    std::string segment_name_;
    uint32_t segment_number_;
};

struct PathTemplate {
    std::vector<FixedPathSegmentValidator> fixed_segments_;
    // TODO: does it have to be a pointer?
    std::vector<std::unique_ptr<TemplatedPathSegmentValidator>> templated_segments_;
};

using AllowedPaths = absl::flat_hash_map<uint32_t, std::vector<std::unique_ptr<PathTemplate>>>;

enum class PathValidationResult {
    MATCHED,
    MATCHED_WITH_ERRORS,
    NOT_MATCHED
};

std::pair<bool, absl::optional<std::string>>
validatePath(const AllowedPaths& allowed_paths, absl::string_view path);

std::pair<PathValidationResult, absl::optional<std::string>>
checkPath(const PathTemplate& path_template, const std::vector<absl::string_view>& path_segments);

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
