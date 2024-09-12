#include "contrib/payload_validator/filters/http/source/validator.h"

//#include "envoy/stats/scope.h"
//#include "envoy/stats/stats.h"
//##include "envoy/stats/stats_macros.h"

//#include "source/extensions/filters/http/common/factory_base.h"

//#include "contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.h"
//#include
//"contrib/envoy/extensions/filters/http/payload_validator/v3/payload_validator.pb.validate.h"

using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

bool JSONValidator::initializeValidator(const std::string& schema) {
  // Convert schema string to nlohmann::json object.
  json schema_as_json;
  try {
    schema_as_json = json::parse(schema);
  } catch (...) {
    return false;
  }
  // Schema seems to be a valid json doc, but it does not mean it describes
  // proper json schema.
  // TODO: get the return value and return false if schema is not accepted.
  validator_.set_root_schema(schema_as_json);

  return true;
}

bool ParamValidator::initialize(const std::string& schema) {
  // Create json schema which consists of param name and param expected schema.
  std::string param_schema = R"EOF(
  {
  "type": "object",
  "properties": {
    ")EOF" + param_name_ +
                             "\":" + schema + "}}";

  std::cerr << param_schema << "\n";

  // TODO: check if the schema is integer or string.

  return initializeValidator(param_schema);
}

std::pair<bool, absl::optional<std::string>>
ParamValidator::validate(absl::string_view param_value) {
  // Create a JSON object based on parameter's value.
  // std::string to_test = "{\"" + std::string(param_name_) + "\":\"" + std::string(param_value) +
  // "\"}";
  // TODO: if schema is integer, do not add quotes to the value. If it is a string, add quotes.
  std::string to_test = "{\"" + std::string(param_name_) + "\":" + std::string(param_value) + "}";
  std::cerr << to_test << "\n";

  json param_as_json;
  try {
    param_as_json = json::parse(to_test);
  } catch (...) {
    // TODO: This should never fail as the string to validate is created by the programmer.
    // Check what happens if the param_value or param_name contains invalid characters
    // like " or { or }.
    ASSERT(false);
  }

  try {
    validator_.validate(param_as_json);
    // No error.
  } catch (const std::exception& e) {
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             absl::optional<std::string>(e.what()));
    std::cerr << "URL param does not match the schema, here is why: " << e.what() << "\n";
    // ASSERT(false);
  }
  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

std::pair<bool, absl::optional<std::string>> validateParams(
    const absl::flat_hash_map<std::string, std::unique_ptr<ParamValidator>>& param_validators,
    const absl::string_view url) {
  // const absl::string_view url = headers.getPathValue();
  absl::flat_hash_set<std::string> required_params;
  for (const auto& param_validator : param_validators) {
    if (param_validator.second->required()) {
      required_params.emplace(param_validator.first);
    }
  }

  std::cerr << "PATH is " << url << "\n";
  auto start = url.find('?');
  if (start == absl::string_view::npos) {
    // TODO: no params. Check if all params are optional (not required).
    // for (const auto& param_validator : param_validators) {
    // if (param_validator.second->required()) {
    if (!required_params.empty()) {
      // TODO: construct list of required params.
      // auto v = std::set<std::string>{"1", "2", "3"};
      return std::make_pair<bool, absl::optional<std::string>>(
          false, absl::optional<std::string>(fmt::format("Missing Required query parameter(s): {}",
                                                         // param_validator.first)));
                                                         //(*required_params.begin())
                                                         // fmt::join(v, " ")
                                                         fmt::join(required_params, " "))));
    }

    //}
    // There are no required params.
    return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
  }
  const absl::string_view params = url.substr(start + 1);
  // Split all params by '&';
  // auto param_tokens = absl::StrSplit(params, '&');
  std::vector<absl::string_view> param_tokens = absl::StrSplit(params, '&');

  // absl::InlinedVector<absl::string_view, 5> validated_params;
  //  Several scenarios:
  //  - check if all required params were present.
  //  - check if there are extra not-expected params
  for (auto param : param_tokens) {
    auto name_end = param.find('=');
    const absl::string_view param_name = param.substr(0, name_end);
    const absl::string_view param_value = param.substr(name_end + 1);

    std::cerr << "PARAM is " << param_name << "-" << param_value << "\n";

    // Get validator for the param_name.
    const auto& param_validator = param_validators.find(param_name);
    if (param_validator == param_validators.end()) {
#if 0
      local_reply_ = true;
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
        fmt::format("Unexpected parameter: {}",
                    param_name),
                                         nullptr, absl::nullopt, "");
      config_.stats()->requests_validation_failed_.inc();
      config_.stats()->requests_validation_failed_enforced_.inc();
      return Http::FilterHeadersStatus::StopIteration;
#endif
      return std::make_pair<bool, absl::optional<std::string>>(
          false,
          absl::optional<std::string>(fmt::format("Unexpected query parameter: {}", param_name)));
    }
    auto result = (*param_validator).second->validate(param_value);

    if (!result.first) {
#if 0
      local_reply_ = true;
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
        fmt::format("Validation of parameter {} failed: {}",
                    param_name,
                    result.second.value()),
                                         nullptr, absl::nullopt, "");
      config_.stats()->requests_validation_failed_.inc();
      config_.stats()->requests_validation_failed_enforced_.inc();
      return Http::FilterHeadersStatus::StopIteration;
#endif
      return std::make_pair<bool, absl::optional<std::string>>(
          false, absl::optional<std::string>("Something went wrong with params"));
    }

    if ((*param_validator).second->required()) {
      required_params.erase((*param_validator).first);
    }

    // validated_params.push_back(param_name);
  }

  // Check if all required params were checked.
  if (!required_params.empty()) {
    // TODO: create list of missing params.
    return std::make_pair<bool, absl::optional<std::string>>(
        false, absl::optional<std::string>(fmt::format("Required query parameter is missing: {}",
                                                       // param_validator.first)));
                                                       //(*required_params.begin())
                                                       fmt::join(required_params, " "))));
  }

  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
