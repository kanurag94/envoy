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

class PayloadValidatorDataTests : public ::testing::Test {
public:
  PayloadValidatorDataTests() {
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

    envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
    TestUtility::loadFromYaml(yaml, config);

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
  }

  std::unique_ptr<FilterConfig> filter_config_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::unique_ptr<Filter> test_filter_;
  Buffer::OwnedImpl data_;
  bool buffered_{false};
};

// Test configuration of requests
TEST_F(PayloadValidatorDataTests, ValidateRequestMethod) {

  Http::TestRequestHeaderMapImpl test_headers;

  // POST and GET should be accepted.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));
  test_headers.setMethod(Http::Headers::get().MethodValues.Get);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

  // PUT should not be accepted. Callback to send local reply should be called.
  test_headers.setMethod(Http::Headers::get().MethodValues.Put);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::MethodNotAllowed, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_headers, true));
}

TEST_F(PayloadValidatorDataTests, ValidateRequestBody) {
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

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

TEST_F(PayloadValidatorDataTests, ValidateChunkedRequestBody) {
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

  std::string body_part1 = "{\"foo\":";
  std::string body_part2 = "\"value\"}";
  Buffer::OwnedImpl data;

  data.add(body_part1);
  ASSERT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, test_filter_->decodeData(data, false));

  data.drain(data.length());
  data.add(body_part2);
  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorDataTests, RejectTooLargeBody) {
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

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

TEST_F(PayloadValidatorDataTests, RejectTooLargeBodyChunked) {
  Http::TestRequestHeaderMapImpl test_headers;

  // Header decoding is necessary to select proper body validator.
  test_headers.setMethod(Http::Headers::get().MethodValues.Post);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_headers, true));

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

TEST_F(PayloadValidatorDataTests, RejectGetWithPayload) {
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

TEST_F(PayloadValidatorDataTests, SendLocalDoesNotRunValidator) {
  // TODO: make sure that send local skips any processing on receive path.
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
