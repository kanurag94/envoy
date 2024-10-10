#pragma once

#include <nlohmann/json-schema.hpp>
#include <string>

#include "envoy/server/filter_config.h"

//#include "envoy/stats/scope.h"
//#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.h"
#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"
#include "contrib/payload_validator/filters/http/source/validator.h"

using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

#if 0 // This should be deleted
class PayloadDescription {
public:
  PayloadDescription(const std::string type) : type_(type), max_size_(1024 * 1024) {}
  // Returns a pair. If the boolean is true, it means that payload
  // passed validation.
  // If the boolean in the pair is false, the second value in the string
  // which describes the error.
  virtual std::pair<bool, absl::optional<std::string>> validate(const Buffer::Instance&) PURE;
  virtual ~PayloadDescription() {}
  uint32_t maxSize() const { return max_size_; }
  void setMaxSize(uint32_t max_size) { max_size_ = max_size; }
  bool active() const { return active_; }

protected:
  std::string type_;
  uint32_t max_size_;
  // Validator has been initialized with schema definition.
  bool active_{false};
};

class JSONPayloadDescription : public PayloadDescription {
public:
  JSONPayloadDescription() : PayloadDescription("application/json") {}
  std::pair<bool, absl::optional<std::string>> validate(const Buffer::Instance&) override;

  bool initialize(const std::string& schema);

private:
  json_validator validator_;
};
#endif // to be deleted

struct Operation {
public:
  const std::unique_ptr<JSONBodyValidator>& getRequestValidator() const { return request_; }
  const std::shared_ptr<JSONBodyValidator> getResponseValidator(uint32_t code) const;

  absl::flat_hash_map<std::string, std::unique_ptr<QueryParamValidator>> params_;
  std::unique_ptr<JSONBodyValidator> request_;
  absl::flat_hash_map</*code*/ uint32_t, std::shared_ptr<JSONBodyValidator>> responses_;

private:
};


#define ALL_PAYLOAD_VALIDATOR_STATS(COUNTER)                                                       \
  COUNTER(requests_validated)                                                                      \
  COUNTER(requests_validation_failed)                                                              \
  COUNTER(requests_validation_failed_enforced)                                                     \
  COUNTER(responses_validated)                                                                     \
  COUNTER(responses_validation_failed)                                                             \
  COUNTER(responses_validation_failed_enforced)

struct PayloadValidatorStats {
  ALL_PAYLOAD_VALIDATOR_STATS(GENERATE_COUNTER_STRUCT)
};

// Path structure binds a top OpenAPI construct: url's path with 
// other attributes defined for that path: method, body and query parameters.
struct Path {
    // path template is matched first. If matches, operations, params and body matching
    // follows.
    PathTemplateValidator path_template_;
    // operations are matched only when matching path was successful.
    absl::flat_hash_map<std::string, std::shared_ptr<Operation>> operations_;
  const std::shared_ptr<Operation> getOperation(const std::string& name) const;
};

/**
 * Config registration for http payload validator filter.
 */
class FilterConfig {
public:
  FilterConfig(const std::string& stats_prefix, Stats::Scope& scope)
      : scope_(scope),
        stats_(std::make_shared<PayloadValidatorStats>(generateStats(stats_prefix, scope_))),
        max_size_(2*1024*1024 /* default max size is 2MB */) {}
  json_validator& getValidator() { return validator_; }

std::pair<bool, absl::optional<std::string>>
  processConfig(const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator&
                    proto_config);
  std::shared_ptr<PayloadValidatorStats> stats() const { return stats_; }
  void setStatsStoreForTest(const std::string& prefix, Stats::Scope& scope) {
    stats_ = std::make_shared<PayloadValidatorStats>(generateStats(prefix, scope));
  }

  PayloadValidatorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return PayloadValidatorStats{ALL_PAYLOAD_VALIDATOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  const std::vector<Path>& getPaths() const {return paths_;}

  uint32_t maxSize() const { return max_size_; }

  Stats::Scope& scope_;
  std::shared_ptr<PayloadValidatorStats> stats_;
  std::string stat_prefix_;

public:
  // TODO: this cannot be public.
  json_validator validator_;
  // Allowed paths and operations.
  std::vector<Path> paths_;
  std::shared_ptr<Operation> empty_{};
  uint32_t max_size_;
};

class FilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::payload_validator::v3::PayloadValidator> {
public:
  FilterConfigFactory() : FactoryBase("envoy.filters.http.payload_validator") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
