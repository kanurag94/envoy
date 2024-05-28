#pragma once

#include <nlohmann/json-schema.hpp>
#include <string>

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.h"
#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"

using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

class PayloadDescription {
public:
  PayloadDescription(const std::string type) : type_(type) {}
  // Returns a pair. If the boolean is true, it means that payload
  // passed validation.
  // If the boolean in the pair is false, the second value in the string
  // which describes the error.
  virtual std::pair<bool, absl::optional<std::string>> validate(Buffer::Instance&) PURE;
  virtual ~PayloadDescription() {}

private:
  std::string type_;
};

class JSONPayloadDescription : public PayloadDescription {
public:
  JSONPayloadDescription() : PayloadDescription("application/json") {}
  std::pair<bool, absl::optional<std::string>> validate(Buffer::Instance&) override;

  bool initialize(const std::string& schema);

private:
  json_validator validator_;
};

struct Operation {
public:
  const std::unique_ptr<PayloadDescription>& getRequestValidator() const { return request_; }
  const std::unique_ptr<PayloadDescription>& getResponseValidator(uint32_t code) const;

  std::unique_ptr<PayloadDescription> request_;
  absl::flat_hash_map</*code*/ uint32_t, std::unique_ptr<PayloadDescription>> responses_;

private:
  std::unique_ptr<PayloadDescription> empty_{};
};

/**
 * Config registration for http payload validator filter.
 */
class FilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::payload_validator::v3::PayloadValidator> {
public:
  FilterConfig() : FactoryBase("envoy.filters.http.payload_validator") {}
  json_validator& getValidator() { return validator_; }

  bool
  processConfig(const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator&
                    proto_config);
  const std::unique_ptr<Operation>& getOperation(const std::string& name) const;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

public:
  json_validator validator_;
  absl::flat_hash_map<std::string, std::unique_ptr<Operation>> operations_;
  std::unique_ptr<Operation> empty_{};
};

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
