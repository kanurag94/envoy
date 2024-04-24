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

Http::FilterFactoryCb FilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config,
    const std::string& /* stats_prefix*/, Server::Configuration::FactoryContext& /*context*/) {

  // to-do. Check if schema is a valid json.
  json schema = json::parse((config.schema()));;
  try {
  //validator_.set_root_schema(person_schema);  
  validator_.set_root_schema(schema);  
    } catch (const std::exception &e) {
    std::cerr << "Validation of schema failed, here is why: " << e.what() << "\n";
    }

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
