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

const std::string main_post_config = R"EOF(
  operations:
  - method: POST
    request_max_size: 100
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
  )EOF";

const std::string get_method_config = R"EOF(
  - method: GET
    request_max_size: 0
  )EOF";

const std::string empty_200_response_config = R"EOF(
    responses:
    - http_status:
        code: 200
  )EOF";

const std::string payload_for_200_response_config = R"EOF(
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

class PayloadValidatorFilterTests : public ::testing::Test {
public:
  void initialize(const std::string config_string) {
#if 0
    const std::string yaml = R"EOF(
  operations:
  - method: POST  
    request_max_size: 100
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
    request_max_size: 0
  )EOF";
#endif

    envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
    TestUtility::loadFromYaml(config_string, config);

    // Create filter's config.
    filter_config_ = std::make_unique<FilterConfig>();
    filter_config_->processConfig(config);

    // Create filter based on the created config.
    std::shared_ptr<PayloadValidatorStats> stats = std::make_shared<PayloadValidatorStats>();

    test_filter_ = std::make_unique<Filter>(*filter_config_, stats);
    test_filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault([this]() {
      return buffered_ ? &data_ : nullptr;
    });
    ON_CALL(decoder_callbacks_, addDecodedData(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool) {
          data_.add(data);
          buffered_ = true;
        }));

    test_filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::unique_ptr<FilterConfig> filter_config_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<Filter> test_filter_;
  Buffer::OwnedImpl data_;
  bool buffered_{false};
};

// Test configuration of requests
TEST_F(PayloadValidatorFilterTests, ValidateRequestMethod) {
  initialize(main_post_config + get_method_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // POST with subsequent body should be accepted (without calling sendLocalReply).
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, false));
  // Header-only POST should be rejected. Body must be present.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, true));
  // Header-only GET should be accepted
  test_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

  // PUT should not be accepted. Callback to send local reply should be called.
  test_headers.setMethod(Http::Headers::get().MethodValues.Put);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::MethodNotAllowed, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, true));
}

TEST_F(PayloadValidatorFilterTests, ValidateRequestBody) {
  initialize(main_post_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, false));

  // TODO: separate validator from filter. We are not checking validator here.
  std::string body = "{\"foo\": \"value\"}";
  Buffer::OwnedImpl data;

  data.add(body);

  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->decodeData(data, true));

  // Incorect body - foo should be a string.
  body = "{\"foo\": 100}";
  data.drain(data.length());
  data.add(body);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, ValidateChunkedRequestBody) {
  initialize(main_post_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, false));

  std::string body_part1 = "{\"foo\":";
  std::string body_part2 = "\"value\"}";
  Buffer::OwnedImpl data;

  data.add(body_part1);
  ASSERT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, test_filter_->decodeData(data, false));

  data.drain(data.length());
  data.add(body_part2);
  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectTooLargeBody) {
  initialize(main_post_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, false));

  // Body does not have to be json. It is rejected before passing to json parser.
  std::string body = "abcdefghji";
  Buffer::OwnedImpl data;

  // Create one chunk of data which exceeds max allowed size.
  for (auto i = 0; i < 11; i++) {
    data.add(body);
  }

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  // Should be rejected even when stream is not finished.
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, false));
  // should be rejected when stream is finished.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectTooLargeBodyChunked) {
  initialize(main_post_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, false));

  // Body does not have to be json. It is rejected before passing to json parser.
  std::string body = "abcdefghji";
  Buffer::OwnedImpl data;

  for (auto i = 0; i < 10; i++) {
    data.drain(data.length());
    data.add(body);
    ASSERT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
              test_filter_->decodeData(data, false));
  }

  data.drain(data.length());
  data.add(body);
  // Next attempt to buffer chunk of data should be rejected, as it exceeds maximum.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, false));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectGetWithPayload) {
  initialize(main_post_config + get_method_config);
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

  // Body does not have to be json. It is rejected before passing to json parser.
  std::string body = "a";
  Buffer::OwnedImpl data;
  data.add(body);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, SendLocalDoesNotRunValidator) {
  // TODO: make sure that send local skips any processing on receive path.
}

TEST_F(PayloadValidatorFilterTests, RejectInvalidHttpReponse) {
  initialize(main_post_config);
  Http::TestResponseHeaderMapImpl test_headers;
  // Remove the status header.
  test_headers.removeStatus();

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_headers, true));
}

TEST_F(PayloadValidatorFilterTests, PassAllResponsesIfNothingConfigured) {
  initialize(main_post_config + get_method_config);

  // Header decoding is necessary to select proper body validator.
  Http::TestRequestHeaderMapImpl test_request_headers;
  test_request_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers, true));

  Http::TestResponseHeaderMapImpl test_response_headers;
  test_response_headers.setStatus("404");

  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->encodeHeaders(test_response_headers, true));
}

TEST_F(PayloadValidatorFilterTests, PassOnlyConfiguredResponses) {
  initialize(main_post_config + get_method_config + empty_200_response_config);

  // Header decoding is necessary to select proper body validator.
  Http::TestRequestHeaderMapImpl test_request_headers;
  test_request_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers, true));

  Http::TestResponseHeaderMapImpl test_response_headers;

  // 200 response is allowed, because it is configured.
  test_response_headers.setStatus("200");
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->encodeHeaders(test_response_headers, true));

  // 202 is not allowed.
  test_response_headers.setStatus("202");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_response_headers, true));
}

TEST_F(PayloadValidatorFilterTests, CheckResponsePayload) {
  initialize(main_post_config + get_method_config + empty_200_response_config +
             payload_for_200_response_config);

  // Header decoding is necessary to select proper body validator.
  Http::TestRequestHeaderMapImpl test_request_headers;
  test_request_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers, true));

  Http::TestResponseHeaderMapImpl test_response_headers;

  // Send again headers, but without stream end.
  test_response_headers.setStatus("200");
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_response_headers, false));

  // Send correct body.
  std::string body = "{\"foo\": \"value\"}";
  Buffer::OwnedImpl data;

  data.add(body);

  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->encodeData(data, true));

  // Send incorrect body.
  data.add("blahblah");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->encodeData(data, true));
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
