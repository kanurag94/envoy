#include "contrib/payload_validator/filters/http/source/validator.h"

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
  // For example, it type is blahblahblah., it should reject it.
  validator_.set_root_schema(schema_as_json);

  return true;
}

bool ParamValidatorBase::initialize(const std::string& schema) {
  // Schema should be a valid JSON.
  json schema_as_json;
  try {
    schema_as_json = json::parse(schema);
  } catch (...) {
    return false;
  }

  // Check if the type is integer.
  // TODO: type can also be returned as array. This is not handled here yet.
    auto type = schema_as_json.find("type");
        if (type == schema_as_json.end()) {
        return false;
    }

    if (type.value() == "string") {
        value_decorator_ = "\"";
    }
    
  // Create json schema which consists of param name and param expected schema.
  std::string param_schema = R"EOF(
  {
  "type": "object",
  "properties": {
    ")EOF" + param_name_ +
                             "\":" + schema + "}}";

  std::cerr << param_schema << "\n";

  return initializeValidator(param_schema);
}

std::pair<bool, absl::optional<std::string>>
ParamValidatorBase::validate(absl::string_view param_value) {
  // Create a JSON object based on parameter's value.
  std::string to_test = "{\"" + std::string(param_name_) + "\":" + value_decorator_ + std::string(param_value) + value_decorator_ + "}";
  std::cerr << to_test << "\n";

  json param_as_json;
  try {
    param_as_json = json::parse(to_test);
  } catch (...) {
    // Parameter contains unexpected characters which cause to_test to fail json syntax check.
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             fmt::format("Parameter {} contains unexpected characters", param_name_));
  }

  try {
    validator_.validate(param_as_json);
    // No error.
  } catch (const std::exception& e) {
    return std::make_pair<bool, absl::optional<std::string>>(false,
    fmt::format("Parameter {} does not match the schema: {}", param_name_, e.what()));
  }
  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

std::pair<bool, absl::optional<std::string>> validateParams(
    const absl::flat_hash_map<std::string, std::unique_ptr<QueryParamValidator>>& param_validators,
    const absl::string_view url) {

  // This is temporary storage to store all required query parameters.
  // When url is processed, parameters found in url are removed from this storage.
  // Ath the end of processing of url, the storage should be empty, which means
  // that all required parameters were found in the url.

  // This is temporary storage to store all required query parameters.
  // When url is processed, parameters found in url are removed from this storage.
  // Ath the end of processing of url, the storage should be empty, which means
  // that all required parameters were found in the url.
  absl::flat_hash_set<std::string> required_params;
  for (const auto& param_validator : param_validators) {
    if (param_validator.second->required()) {
      required_params.emplace(param_validator.first);
    }
  }

  auto start = url.find('?');
  if (start == absl::string_view::npos) {
    if (!required_params.empty()) {
      // Url does not contain any parameters and there are mandatory parameters.
      return std::make_pair<bool, absl::optional<std::string>>(
          false, absl::optional<std::string>(fmt::format("Missing required query parameter(s): {}",
                                                         fmt::join(required_params, " "))));
    }

    // There are no required params.
    return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
  }

  const absl::string_view params = url.substr(start + 1);
  // Split all params by '&' to separate individual parameters.
  std::vector<absl::string_view> param_tokens = absl::StrSplit(params, '&');

  //  Handle the following scenarios:
  //  - check if all required params were present.
  //  - check if there are extra not-expected params
  for (auto param : param_tokens) {
    // Split the parameter to separate value.
    auto name_end = param.find('=');
    // handle parameters without value, where just a presence is enough.
    if (name_end != absl::string_view::npos) {
      const absl::string_view param_name = param.substr(0, name_end);
      const absl::string_view param_value = param.substr(name_end + 1);

      // Get validator for the param_name.
      const auto& param_validator = param_validators.find(param_name);
      if (param_validator == param_validators.end()) {
        return std::make_pair<bool, absl::optional<std::string>>(
            false,
            absl::optional<std::string>(fmt::format("Unexpected query parameter: {}", param_name)));
      }

      // Call the validator passing the parameters's value.
      auto result = (*param_validator).second->validate(param_value);

      if (!result.first) {
        return std::make_pair<bool, absl::optional<std::string>>(
            false, absl::optional<std::string>(fmt::format("Validation of parameter {} failed: {}", param_name, result.second.value())));
      }

    // Mark that required parameter was processed.
    if ((*param_validator).second->required()) {
      required_params.erase((*param_validator).first);
    }
    } else {
        // TODO: handle case where parameter does not need value.
        // Check if it is market in config as "no-value-required" and handle 
        // the case if value is indeed required and check if it was required param and 
        // remove it from required_params storage.
    }
  }

  // Check if all required params were checked.
  if (!required_params.empty()) {
    return std::make_pair<bool, absl::optional<std::string>>(
        false, absl::optional<std::string>(fmt::format("Required query parameter is missing: {}",
                                                       fmt::join(required_params, " "))));
  }

  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

// Function checks if given path (broken into segments) match the template.
// Function should not be called unless the number of segments is equal to the number
// of template segments.
std::pair<PathValidationResult, absl::optional<std::string>>
checkPath(const PathTemplateValidator& path_template, const std::vector<absl::string_view>& path_segments) {
    
    ASSERT(path_segments.size() == (path_template.fixed_segments_.size() + path_template.templated_segments_.size()));

        // Iterate over all fixed segments.
        for (auto& fixed_segment : path_template.fixed_segments_) {
            if (!fixed_segment.validate(path_segments[fixed_segment.segmentNumber()])) {
          return std::make_pair<PathValidationResult, absl::optional<std::string>>(
            PathValidationResult::NOT_MATCHED,
            absl::optional<std::string>(fmt::format("Fixed parts of template do not match.")));
            }
        }

        // Fixed segments of the path matched. 
        // Now try templated segments.
        for (auto& templated_segment : path_template.templated_segments_) {
            auto result = templated_segment->validate(path_segments[templated_segment->segmentNumber()]);
            if (!result.first) {
          return std::make_pair<PathValidationResult, absl::optional<std::string>>(
            PathValidationResult::MATCHED_WITH_ERRORS,
            std::move(result.second));
            }
        }

      return std::make_pair<PathValidationResult, absl::optional<std::string>>(
          PathValidationResult::MATCHED,
          absl::nullopt);
}

bool JSONBodyValidator::initialize(const std::string& schema) {
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
JSONBodyValidator::validate(absl::string_view body) {
  json rec_buf;
  try {
    rec_buf = json::parse(body);
  } catch (const std::exception& e) {
    // Payload is not valid JSON.
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             fmt::format("Payload is not a valid JSON document: {}", e.what()));
  }

  try {
    validator_.validate(rec_buf);
    // No error.
  } catch (const std::exception& e) {
    return std::make_pair<bool, absl::optional<std::string>>(false,
                                                             fmt::format("Payload does not match the schema: {}", e.what()));
  }

  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt);
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
