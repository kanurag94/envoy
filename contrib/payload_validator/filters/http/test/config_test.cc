#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
  ASSERT_TRUE(filter_config.processConfig(config));

  // For POST, there should be a validator.
  auto& operation = filter_config.getOperation("POST");
  ASSERT_TRUE(operation != nullptr);
  ASSERT_TRUE(operation->getRequestValidator() != nullptr);

  // For GET, the validator should not exist.
  auto& operation1 = filter_config.getOperation("GET");
  ASSERT_TRUE(operation1 != nullptr);
  ASSERT_FALSE(operation1->getRequestValidator()->active());

  // For DELETE, nothing was defined, so entire operation's context should not be found.
  auto& operation2 = filter_config.getOperation("DELETE");
  ASSERT_TRUE(operation2 == nullptr);
}

TEST(PayloadValidatorConfigTests, RequestAndResponseConfig) {
  const std::string yaml = R"EOF(
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
  ASSERT_TRUE(filter_config.processConfig(config));

  // For POST, there should be a validator.
  auto& operation = filter_config.getOperation("POST");
  ASSERT_TRUE(operation != nullptr);

  // Get the validator for code 200 and 202.
  ASSERT_TRUE(operation->getResponseValidator(200) != nullptr);
  ASSERT_TRUE(operation->getResponseValidator(202) != nullptr);

  // There should be no validator for code 205.
  ASSERT_TRUE(operation->getResponseValidator(205) == nullptr);
}

TEST(PayloadValidatorConfigTests, InvalidConfigs) {
  // const std::string yaml = "";
  envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
  // TestUtility::loadFromYaml(yaml, config);

  testing::NiceMock<Stats::MockStore> store;
  Stats::MockScope& scope{store.mockScope()};
  // Create filter's config.
  FilterConfig filter_config("test_stats", scope);
  ASSERT_FALSE(filter_config.processConfig(config));
}

TEST(PayloadValidatorConfigTests, InvalidConfigs1) {
  // const std::string yaml = "";
  const std::string yaml = R"EOF(
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
  ASSERT_FALSE(filter_config.processConfig(config));
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
