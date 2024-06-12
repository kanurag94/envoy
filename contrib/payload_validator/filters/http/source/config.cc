#include "contrib/payload_validator/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"
#include "contrib/payload_validator/filters/http/source/filter.h"

using nlohmann::json;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

const std::unique_ptr<PayloadDescription>& Operation::getResponseValidator(uint32_t code) const {
  auto it = responses_.find(code);

  if (it == responses_.end()) {
    return empty_;
  }

  return (*it).second;
}

bool JSONPayloadDescription::initialize(const std::string& schema) {
  // Convert schema string to nlohmann::json object.
  json schema_as_json;
  try {
    schema_as_json = json::parse(schema);
  } catch (...) {
    return false;
  }
  // Schema seems to be a valid json doc, but it does not mean it describes
  // proper json schema.

  validator_.set_root_schema(schema_as_json);
  return true;
}

std::pair<bool, absl::optional<std::string>>
JSONPayloadDescription::validate(const Buffer::Instance& data) {
  std::string message;
  message.assign(std::string(
      static_cast<char*>((const_cast<Buffer::Instance&>(data)).linearize(data.length())),
      data.length()));

  // Todo (reject if this is not json).
  json rec_buf = json::parse(message);

  try {
    validator_.validate(rec_buf);
    // No error.
  } catch (const std::exception& e) {
    std::cerr << "Payload does not match the schema, here is why: " << e.what() << "\n";
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             absl::optional<std::string>(e.what()));
  }

  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

bool FilterConfig::processConfig(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config) {
  // bool request_found = false;
  // bool response_found = false;

  // iterate over configured operations.
  for (const auto& operation : config.operations()) {
    // const auto& method = operation.method();
    auto new_operation = std::make_shared<Operation>();

    auto request_validator = std::make_unique<JSONPayloadDescription>();

    if (operation.has_request_max_size()) {
      request_validator->setMaxSize(operation.request_max_size().value());
    }

    if (!operation.request_body().schema().empty()) {

      if (!request_validator->initialize(operation.request_body().schema())) {
        return false;
      }

      //  request_found = true;
    }
    new_operation->request_ = std::move(request_validator);

    // Iterate over response codes and their expected formats.
    for (const auto& response : operation.responses()) {
      auto code = response.code();

      if (!response.response_body().schema().empty()) {
        auto response_validator = std::make_unique<JSONPayloadDescription>();
        if (!response_validator->initialize(operation.request_body().schema())) {
          return false;
        }

        new_operation->responses_.emplace(code, std::move(response_validator));
        //   response_found = true;
      }
    }

    std::string method = envoy::config::core::v3::RequestMethod_Name(operation.method());
    operations_.emplace(method, std::move(new_operation));
  }

  /*
    if (!(request_found || response_found)) {
      return false;
    }
  */

  return true;
}

// Find context related to method.
const std::shared_ptr<Operation> FilterConfig::getOperation(const std::string& name) const {
  const auto it = operations_.find(name);

  if (it == operations_.end()) {

    return empty_;
  }

  return (*it).second;
}

Http::FilterFactoryCb FilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config,
    const std::string& /* stats_prefix*/, Server::Configuration::FactoryContext& /*context*/) {

  if (!processConfig(config)) {
    throw EnvoyException(fmt::format("Invalid payload validator config: {}", "TODO"));
  }

#if 0
  // to-do. Check if schema is a valid json.
  json schema = json::parse((config.schema()));;
  try {
  //validator_.set_root_schema(person_schema);  
  validator_.set_root_schema(schema);  
    } catch (const std::exception &e) {
    std::cerr << "Validation of schema failed, here is why: " << e.what() << "\n";
    }
#endif

  auto stats = std::make_shared<PayloadValidatorStats>();
  return [this, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*this, stats));
  };
}

/**
 * Static registration for the http payload validator filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(FilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.http_payload_validator_filter");

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
