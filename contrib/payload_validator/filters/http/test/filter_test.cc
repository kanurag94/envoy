#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/payload_validator/filters/http/source/config.h"
#include "contrib/payload_validator/filters/http/source/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

// Test configuration of requests
TEST(PayloadValidatorConfigTests, ValidateMethod) {
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

  // Create filter's config.
  FilterConfig filter_config;
  ASSERT_TRUE(filter_config.processConfig(config));

  // Create filter based on the created config.
  std::shared_ptr<PayloadValidatorStats> stats = std::make_shared<PayloadValidatorStats>();

  Filter test_filter(filter_config, stats);
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  test_filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl test_headers;

  // POST and GET should be accepted.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter.decodeHeaders(test_headers, true));
  test_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter.decodeHeaders(test_headers, true));

  // PUT should not be accepted. Callback to send local reply should be called.
  test_headers.setMethod(Http::Headers::get().MethodValues.Put);
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::MethodNotAllowed, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter.decodeHeaders(test_headers, true));
}

TEST(PayloadValidatorConfigTests, ValidateBody) {
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

  // Create filter's config.
  FilterConfig filter_config;
  ASSERT_TRUE(filter_config.processConfig(config));

  // Create filter based on the created config.
  std::shared_ptr<PayloadValidatorStats> stats = std::make_shared<PayloadValidatorStats>();

  Filter test_filter(filter_config, stats);
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  test_filter.setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter.decodeHeaders(test_headers, true));

  // TODO: separate validator from filter. We are not checking validator here.
  std::string body = "{\"foo\": \"value\"}";
  Buffer::OwnedImpl data;

  data.add(body);

  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter.decodeData(data, true));
}
} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
