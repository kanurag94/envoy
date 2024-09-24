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

const std::shared_ptr<PayloadDescription> Operation::getResponseValidator(uint32_t code) const {
  auto it = responses_.find(code);

  if (it == responses_.end()) {
    return nullptr;
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

  active_ = true;
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
  json rec_buf;
  try {
    rec_buf = json::parse(message);
  } catch (const std::exception& e) {
    // Payload is not valid JSON.
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             absl::optional<std::string>(e.what()));
  }

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

  stat_prefix_ = config.stat_prefix();

  if (config.paths().empty()) {
    return false;
  }

  // Iterate over configured paths.
  for (const auto& path : config.paths()) {
    Path new_path;
    absl::string_view request_path = path.path();
    if (request_path[0] != '/') {
        // First character must be forward slash.
        return false;
    }

    
    request_path.remove_prefix(1);
    std::vector<absl::string_view> segments = absl::StrSplit(request_path, '/');

    absl::flat_hash_map<absl::string_view, size_t> params_from_url;

    for (size_t i = 0; i < segments.size(); i++) {
        if (segments[i].front() == '{') {
            if (segments[i].back() != '}') {
                // Config should be rejected.
                ASSERT(false);
            }
            segments[i].remove_prefix(1);
            segments[i].remove_suffix(1);
            // TODO: if it is of zero length -> reject the config.
            if (params_from_url.find(segments[i]) != params_from_url.end()) {
                // TODO: Handle case where a parameter name is repeated.
                // Reject that config. 
                ASSERT(false);
            }
            params_from_url.insert({segments[i], i});
        } else {
        new_path.path_template_.fixed_segments_.emplace_back(segments[i], i);
        }
    }
    
  // iterate over configured operations.
  for (const auto& operation : path.operations()) {
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
      auto code = response.http_status().code();

      if (!response.response_body().schema().empty()) {
        auto response_validator = std::make_shared<JSONPayloadDescription>();
        if (!response_validator->initialize(response.response_body().schema())) {
          return false;
        }

        new_operation->responses_.emplace(code, std::move(response_validator));
        //   response_found = true;
      } else {
        new_operation->responses_.emplace(code, nullptr);
      }
    }
#if 0
  std::string url_schema = R"EOF(
  {
  "type": "object",
  "properties": {
    "admin": {
        "type": "string",
        "enum": ["json", "xml", "yaml"]
    }
  }
  }
  )EOF";
  std::string url_schema = R"EOF(
  {
        "type": "string",
        "enum": ["json", "xml", "yaml"]
  }
  )EOF";
    std::string param_name = "admin";
#endif

    // Add params to be verified for this operation.
    for (const auto& parameter : operation.parameters()) {
      if (parameter.in() ==
          envoy::extensions::filters::http::payload_validator::v3::ParameterLocation::QUERY) {
        std::unique_ptr<ParamValidator> param_validator =
            std::make_unique<ParamValidator>(parameter.name());
        param_validator->initialize(parameter.schema());

        if (parameter.has_required()) {
          param_validator->required(parameter.required().value());
        }

        new_operation->params_.emplace(parameter.name(), std::move(param_validator));
        std::cerr << parameter.name() << "\n";
      }
      
      if (parameter.in() == 
          envoy::extensions::filters::http::payload_validator::v3::ParameterLocation::PATH) {
          
          const auto it = params_from_url.find(parameter.name());
          if (it == params_from_url.end()) {
            // TODO: handle the case where param which should be in the path is not in the path as templated.
            ASSERT(false);
          }

          new_path.path_template_.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>((*it).first, (*it).second));
          new_path.path_template_.templated_segments_.back()->initialize(parameter.schema());
          // Remove it from list of found path params. At the end of params this list should be empty.
          params_from_url.erase(it);
      }
    }

    if (!params_from_url.empty()) {
        // TODO: display the message that not all params defined in path were defined as params.
        ASSERT(false);
    }

#if 0
    auto param_validator = std::make_unique<ParamValidator>(param_name);
    if (!param_validator->initialize(url_schema)) {
      ASSERT(false);
      return false;
    }
    new_operation->params_.emplace("admin", std::move(param_validator));
#endif

    std::string method = envoy::config::core::v3::RequestMethod_Name(operation.method());
    new_path.operations_.emplace(method, std::move(new_operation));
    
    paths_.push_back(std::move(new_path));
  }
    }

  /*
    if (!(request_found || response_found)) {
      return false;
    }
  */

  return true;
}

// Find context related to method.
#if 0
const std::shared_ptr<Operation> FilterConfig::getOperation(const std::string& name) const {
  const auto it = operations_.find(name);

  if (it == operations_.end()) {

    return nullptr;
  }

  return (*it).second;
}
#endif

Http::FilterFactoryCb FilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::payload_validator::v3::PayloadValidator& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  std::cerr << stats_prefix << "\n";
  std::string final_prefix =
      fmt::format("{}payload_validator.{}", stats_prefix, config.stat_prefix());
  std::shared_ptr<FilterConfig> filter_config =
      std::make_shared<FilterConfig>(final_prefix, context.scope());

  if (!filter_config->processConfig(config)) {
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
