#include "contrib/payload_validator/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"
#include "contrib/payload_validator/filters/http/source/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

Http::FilterFactoryCb FilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator&,
    const std::string& /* stats_prefix*/, Server::Configuration::FactoryContext& /*context*/) {
  auto stats = std::make_shared<PayloadValidatorStats>();
  return [stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(stats));
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
