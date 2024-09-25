#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fmt/args.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/payload_validator/filters/http/source/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

// Test configuration of requests
TEST(PayloadValidatorConfigTests, RequestOnlyConfig) {
  const std::string yaml = R"EOF(
  paths:
  - path: "/test"
    operations:
    - method: POST  
      request_body:
        schema: |
          {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "title": "A person",
              "properties": {
                  "foo": {
                      "type": "string"
                  }
              },
              "required": [
                  "foo"
              ],
              "type": "object"
          }
    - method: GET  
  )EOF";

  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_TRUE(filter_config.processConfig(config).first);

  // There should be only one path configured.
  const std::vector<Path>& paths = filter_config.getPaths();
  ASSERT_THAT(paths.size(), 1);

  const Path& path = paths.back();
  // The path validator should only have one fixed segment.
  ASSERT_THAT(path.path_template_.fixed_segments_.size(), 1);
  ASSERT_THAT(path.path_template_.templated_segments_.size(), 0);

  // For POST, there should be a validator.
  auto& operation = path.getOperation("POST");
  ASSERT_TRUE(operation != nullptr);
  ASSERT_TRUE(operation->getRequestValidator() != nullptr);

  // For GET, the validator should not exist.
  auto& operation1 = path.getOperation("GET");
  ASSERT_TRUE(operation1 != nullptr);
  ASSERT_FALSE(operation1->getRequestValidator()->active());

  // For DELETE, nothing was defined, so entire operation's context should not be found.
  auto& operation2 = path.getOperation("DELETE");
  ASSERT_TRUE(operation2 == nullptr);
}

TEST(PayloadValidatorConfigTests, RequestAndResponseConfig) {
  const std::string yaml = R"EOF(
  paths:
  - path: "/test/second"
    operations:
    - method: POST  
      request_body:
        schema: |
          {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "title": "A person",
              "properties": {
                  "foo": {
                      "type": "string"
                  }
              },
              "required": [
                  "foo"
              ],
              "type": "object"
          }
      responses:
      - http_status:
          code: 200
        response_body:
          schema: |
            {
                "$schema": "http://json-schema.org/draft-07/schema#",
                "title": "A person",
                "properties": {
                    "foo": {
                        "type": "string"
                    }
                },
                "required": [
                    "foo"
                ],
                "type": "object"
            }
      - http_status:
          code: 202
        response_body:
          schema: |
            {
                "$schema": "http://json-schema.org/draft-07/schema#",
                "title": "A person",
                "properties": {
                    "foo": {
                        "type": "string"
                    }
                },
                "required": [
                    "foo"
                ],
                "type": "object"
            }
  )EOF";

  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_TRUE(filter_config.processConfig(config).first);

  // There should be only one path configured.
  const std::vector<Path>& paths = filter_config.getPaths();
  ASSERT_THAT(paths.size(), 1);

  const Path& path = paths.back();
  // The path validator should have two fixed segments.
  ASSERT_THAT(path.path_template_.fixed_segments_.size(), 2);
  ASSERT_THAT(path.path_template_.templated_segments_.size(), 0);

  // For POST, there should be a validator.
  auto& operation = path.getOperation("POST");
  ASSERT_TRUE(operation != nullptr);

  // Get the validator for code 200 and 202.
  ASSERT_TRUE(operation->getResponseValidator(200) != nullptr);
  ASSERT_TRUE(operation->getResponseValidator(202) != nullptr);

  // There should be no validator for code 205.
  ASSERT_TRUE(operation->getResponseValidator(205) == nullptr);
}

// Test verifies that configuration with parameters
// creates validators for those parameters.
TEST(PayloadValidatorConfigTests, RequestWithParams) {
  const std::string yaml = R"EOF(
  paths:
  - path: "/test"
    operations:
    - method: GET  
      parameters:
      - name: "param1"
        in: QUERY
        required: true
        schema: |
          {
              "type": "string"
          }
      - name: "param2"
        in: QUERY
        required: false
        schema: |
          {
              "type": "integer"
          }
  )EOF";

  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_TRUE(filter_config.processConfig(config).first);

  // There should be only one path configured.
  const std::vector<Path>& paths = filter_config.getPaths();
  ASSERT_THAT(paths.size(), 1);

  const Path& path = paths.back();
  // The path validator should have one fixed segment.
  ASSERT_THAT(path.path_template_.fixed_segments_.size(), 1);

  auto& operation = path.getOperation("GET");
  ASSERT_TRUE(operation != nullptr);

  const auto& params = operation->params_;
  ASSERT_EQ(params.size(), 2);

  // First param.
  auto it = params.find("param1");
  ASSERT_TRUE(it != params.end());

  // Second param.
  it = params.find("param2");
  ASSERT_TRUE(it != params.end());

  // Non-existing param
  it = params.find("param3");
  ASSERT_TRUE(it == params.end());

}

// Test verifies that configuration with path parameters
// creates validators for those path parameters.
TEST(PayloadValidatorConfigTests, RequestWithPathParams) {
  const std::string yaml = R"EOF(
  paths:
  - path: "/users/{id}"
    operations:
    - method: GET  
      parameters:
      - name: "param1"
        in: QUERY
        required: true
        schema: |
          {
              "type": "string"
          }
      - name: "id"
        in: PATH
        schema: |
          {
              "type": "integer"
          }
  )EOF";

  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_TRUE(filter_config.processConfig(config).first);

  // There should be only one path configured.
  const std::vector<Path>& paths = filter_config.getPaths();
  ASSERT_THAT(paths.size(), 1);

  const Path& path = paths.back();
  // The path validator should have one fixed segment and one templated segment.
  ASSERT_THAT(path.path_template_.fixed_segments_.size(), 1);
  ASSERT_THAT(path.path_template_.templated_segments_.size(), 1);

  auto& operation = path.getOperation("GET");
  ASSERT_TRUE(operation != nullptr);

  // Check for query param.
  const auto& params = operation->params_;
  ASSERT_EQ(params.size(), 1);

  auto it = params.find("param1");
  ASSERT_TRUE(it != params.end());
}

TEST(PayloadValidatorConfigTests, InvalidConfigs) {
  // const std::string yaml = "";
  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  // TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_FALSE(filter_config.processConfig(config).first);
}

class WrongConfigTest : public testing::TestWithParam<std::vector<std::string>> {
};

TEST_P(WrongConfigTest, RejectWrongConfig) {
fmt::dynamic_format_arg_store<fmt::format_context> store;

std::string format = "";

    for (const auto& part: GetParam()) {
        store.push_back(part);
        format += "{}";
    } 
    std::string yaml = fmt::vformat(format, store);

  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> stats_store;
  Stats::MockScope& scope{stats_store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_FALSE(filter_config.processConfig(config).first);
}

// Tests passes various wrong configs to execute different paths in config
// processing routine.

// Various parts of config.
const std::string paths_header = R"EOF(
  paths:
  )EOF";

const std::string path_without_leading_slash = R"EOF(
  - path: "test"
)EOF";

const std::string path_no_closing_bracket = R"EOF(
  - path: "/test/{path_param/test"
)EOF";

const std::string path_empty_param_name = R"EOF(
  - path: "/test/{}/test"
)EOF";

const std::string path_repeated_param = R"EOF(
  - path: "/test/{id}/test/{id}"
)EOF";

const std::string path_correct = R"EOF(
  - path: "/test"
)EOF";

const std::string operations_header = R"EOF(
    operations:
  )EOF";

const std::string post_method = R"EOF(
    - method: POST  
  )EOF";

// Wrong schema - no closing bracket in foo definition.
const std::string post_wrong_schema = R"EOF(
      request_body:
        schema: |
          {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "title": "A person",
              "properties": {
                  "foo": {
                      "type": "string"
              },
              "required": [
                  "foo"
              ],
              "type": "object"
          }
  )EOF";

const std::string responses_header = R"EOF(
      responses:
  )EOF";

// Wrong schema - no closing bracket in foo definition.
const std::string response_200_wrong_schema = R"EOF(
      - http_status:
          code: 200
        response_body:
          schema: |
            {
                "$schema": "http://json-schema.org/draft-07/schema#",
                "title": "A person",
                "properties": {
                  "foo": {
                      "type": "string"
                },
                "required": [
                    "foo"
                ],
                "type": "object"
            }
  )EOF";

// Wrong schema - extra comma.
const std::string query_param_wrong_schema = R"EOF(
      parameters:
      - name: "param1"
        in: QUERY
        required: true
        schema: |
          {
              "type": "string",
          }
  )EOF";


const std::string path_correct_templated_1 = R"EOF(
  - path: "/test/{param1}"
)EOF";

const std::string path_correct_templated_2 = R"EOF(
  - path: "/test/{param2}"
)EOF";

const std::string path_correct_templated_1_2 = R"EOF(
  - path: "/test/{param1}/{param2}"
)EOF";

const std::string path_param_correct = R"EOF(
      parameters:
      - name: "param1"
        in: PATH
        required: true
        schema: |
          {
              "type": "string"
          }
  )EOF";

const std::string path_param_wrong_schema = R"EOF(
      parameters:
      - name: "param1"
        in: PATH
        required: true
        schema: |
          {
              "type": "string",
          }
  )EOF";

INSTANTIATE_TEST_SUITE_P(WrongConfigTestSuite, WrongConfigTest,
testing::Values(
    std::vector<std::string>({paths_header}),
    std::vector<std::string>({paths_header, path_without_leading_slash}),
    std::vector<std::string>({paths_header, path_no_closing_bracket}),
    std::vector<std::string>({paths_header, path_empty_param_name}),
    std::vector<std::string>({paths_header, path_repeated_param}),
    std::vector<std::string>({paths_header, path_correct, operations_header, post_method, post_wrong_schema}),
    std::vector<std::string>({paths_header, path_correct, operations_header, post_method, responses_header, response_200_wrong_schema}),
    std::vector<std::string>({paths_header, path_correct, operations_header, post_method, query_param_wrong_schema}),
    std::vector<std::string>({paths_header, path_correct_templated_2, operations_header, post_method, path_param_correct}),
    std::vector<std::string>({paths_header, path_correct_templated_1, operations_header, post_method, path_param_wrong_schema}),
    std::vector<std::string>({paths_header, path_correct_templated_1_2, operations_header, post_method, path_param_correct})
    ));


} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
