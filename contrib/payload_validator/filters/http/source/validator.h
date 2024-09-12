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

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
