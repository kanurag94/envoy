#include "contrib/payload_validator/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"
#include "source/common/common/enum_to_int.h"

#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"
#include "contrib/payload_validator/filters/http/source/filter.h"

using nlohmann::json;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

const std::shared_ptr<JSONBodyValidator> Operation::getResponseValidator(uint32_t code) const {
  auto it = responses_.find(code);

  if (it == responses_.end()) {
    return nullptr;
  }

  return (*it).second;
}

std::pair<bool, absl::optional<std::string>> FilterConfig::processConfig(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config) {

  stat_prefix_ = config.stat_prefix();

  if (config.has_enforce()) {
    enforce_ = config.enforce().value();
  }

  if (config.has_hide_details()) {
    hide_details_ = config.hide_details().value();
  }

  if (config.paths().empty()) {
    return std::make_pair<bool, absl::optional<std::string>>(
        false, "At least one path must be configured");
  }

  // Iterate over configured paths.
  for (const auto& path : config.paths()) {
    Path new_path;
    absl::string_view request_path = path.path();
    if (request_path[0] != '/') {
      // First character must be forward slash.
      return std::make_pair<bool, absl::optional<std::string>>(
          false, fmt::format("Path must start with forward slash: {}", request_path));
    }

    new_path.path_template_.full_path_ = request_path;

    request_path.remove_prefix(1);
    std::vector<absl::string_view> segments = absl::StrSplit(request_path, '/');

    absl::flat_hash_map<absl::string_view, size_t> params_from_url;

    for (size_t i = 0; i < segments.size(); i++) {
      if (segments[i].front() == '{') {
        if (segments[i].back() != '}') {
          // Not closed param name.
          return std::make_pair<bool, absl::optional<std::string>>(
              false, fmt::format("Missing closing bracket for path parameter {}", segments[i]));
        }
        segments[i].remove_prefix(1);
        segments[i].remove_suffix(1);
        if (segments[i].empty()) {
          // Parameter name cannot be empty.
          return std::make_pair<bool, absl::optional<std::string>>(
              false, fmt::format("Empty path parameter in {}", request_path));
        }
        if (params_from_url.find(segments[i]) != params_from_url.end()) {
          // Parameter name is repeated. Reject that config.
          return std::make_pair<bool, absl::optional<std::string>>(
              false, fmt::format("Repeated path parameter {} in {}", segments[i], request_path));
        }
        params_from_url.insert({segments[i], i});
      } else {
        new_path.path_template_.fixed_segments_.emplace_back(segments[i], i);
      }
    }

    // Iterate over configured operations.
    for (const auto& operation : path.operations()) {
      auto new_operation = std::make_shared<Operation>();

      auto request_validator = std::make_unique<JSONBodyValidator>();

      if (!operation.request_body().schema().empty()) {

        try {
          bool init = request_validator->initialize(operation.request_body().schema());
          if (!init) {
            return std::make_pair<bool, absl::optional<std::string>>(
                false, fmt::format("Invalid payload schema for method {} in path {}",
                                   envoy::config::core::v3::RequestMethod_Name(operation.method()),
                                   request_path));
          }
        } catch (const std::exception& e) {
          return std::make_pair<bool, absl::optional<std::string>>(
              false, fmt::format("Invalid payload schema for method {} in path {}: {}",
                                 envoy::config::core::v3::RequestMethod_Name(operation.method()),
                                 request_path, e.what()));
        }
      }

      new_operation->request_ = std::move(request_validator);

      // Iterate over response codes and their expected formats.
      for (const auto& response : operation.responses()) {
        auto code = enumToInt(response.http_status().code());

        if (!response.response_body().schema().empty()) {
          auto response_validator = std::make_shared<JSONBodyValidator>();
          if (!response_validator->initialize(response.response_body().schema())) {
            return std::make_pair<bool, absl::optional<std::string>>(
                false, fmt::format("Invalid response payload schema for code {} in path {}", code,
                                   request_path));
          }

          new_operation->responses_.emplace(code, std::move(response_validator));
        } else {
          new_operation->responses_.emplace(code, nullptr);
        }
      }

      // Add params to be verified for this operation.
      for (const auto& parameter : operation.parameters()) {
        if (parameter.in() ==
            envoy::extensions::filters::http::payload_validator::v3::ParameterLocation::QUERY) {
          std::unique_ptr<QueryParamValidator> param_validator =
              std::make_unique<QueryParamValidator>(parameter.name());
          if (!param_validator->initialize(parameter.schema())) {
            return std::make_pair<bool, absl::optional<std::string>>(
                false, fmt::format("Invalid schema for query parameter {} in path {}",
                                   parameter.name(), request_path));
          }

          if (parameter.has_required()) {
            param_validator->required(parameter.required().value());
          }

          new_operation->params_.emplace(parameter.name(), std::move(param_validator));
        }

        if (parameter.in() ==
            envoy::extensions::filters::http::payload_validator::v3::ParameterLocation::PATH) {

          const auto it = params_from_url.find(parameter.name());
          if (it == params_from_url.end()) {
            // Defined param is not in the path as templated.
            return std::make_pair<bool, absl::optional<std::string>>(
                false, fmt::format("Path parameter {} not found in path {}", parameter.name(),
                                   request_path));
          }

          new_path.path_template_.templated_segments_.push_back(
              std::make_unique<TemplatedPathSegmentValidator>(std::string((*it).first),
                                                              (*it).second));
          if (!new_path.path_template_.templated_segments_.back()->initialize(parameter.schema())) {
            return std::make_pair<bool, absl::optional<std::string>>(
                false, fmt::format("Invalid schema for path parameter {} in path {}",
                                   parameter.name(), request_path));
          }
          // Remove it from list of found path params. At the end of params this list should be
          // empty.
          params_from_url.erase(it);
        }
      }

      if (!params_from_url.empty()) {
        // Not all params defined in path were defined as params.
        return std::make_pair<bool, absl::optional<std::string>>(
            false, fmt::format("Not all path parameters in path {} are defined", request_path));
      }

      std::string method = envoy::config::core::v3::RequestMethod_Name(operation.method());
      new_path.operations_.emplace(method, std::move(new_operation));
    }
    paths_.push_back(std::move(new_path));
  }

  if (config.has_max_size()) {
    max_size_ = config.max_size().value();
  }

  return std::make_pair<bool, absl::optional<std::string>>(true, absl::nullopt);
}

// Find context related to method.
const std::shared_ptr<Operation> Path::getOperation(const std::string& name) const {
  const auto it = operations_.find(name);

  if (it == operations_.end()) {

    return nullptr;
  }

  return (*it).second;
}

Http::FilterFactoryCb FilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  std::string final_prefix =
      fmt::format("{}payload_validator.{}", stats_prefix, config.stat_prefix());
  std::shared_ptr<FilterConfig> filter_config =
      std::make_shared<FilterConfig>(final_prefix, context.scope());

  auto result = filter_config->processConfig(config);

  if (!result.first) {
    throw EnvoyException(
        fmt::format("Invalid payload validator config: {}", result.second.value()));
  }

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*filter_config));
  };
}

/**
 * Static registration for the http payload validator filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(FilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.http_payload_validator_filter");

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
