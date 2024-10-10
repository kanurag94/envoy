#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/payload_validator/filters/http/source/config.h"
#include "contrib/payload_validator/filters/http/source/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

const std::string paths_header_config = R"EOF(
  paths:
  )EOF";

const std::string max_size_100_config = R"EOF(
  max_size: 100
  )EOF";

const std::string max_size_0_config = R"EOF(
  max_size: 0
  )EOF";

const std::string root_path_config = R"EOF(
  - path: "/"
    operations:
  )EOF";

const std::string post_method_config = R"EOF(
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
  )EOF";

const std::string get_method_config = R"EOF(
    - method: GET
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

class PayloadValidatorFilterTestsBase {
public:
  void initialize(const std::string config_string) {
    envoy::extensions::filters::http::payload_validator::v3::PayloadValidator config;
    TestUtility::loadFromYaml(config_string, config);

    // Create filter's config.
    // Create filter based on the created config.
    // std::shared_ptr<PayloadValidatorStats> stats =
    // std::make_shared<PayloadValidatorStats>(generateStats("test_stats", store_.mockScope()));
    ON_CALL(store_, counter("test_stats.requests_validated"))
        .WillByDefault(testing::ReturnRef(requests_validated_));
    ON_CALL(store_, counter("test_stats.requests_validation_failed"))
        .WillByDefault(testing::ReturnRef(requests_validation_failed_));
    ON_CALL(store_, counter("test_stats.requests_validation_failed_enforced"))
        .WillByDefault(testing::ReturnRef(requests_validation_failed_enforced_));
    ON_CALL(store_, counter("test_stats.responses_validated"))
        .WillByDefault(testing::ReturnRef(responses_validated_));
    ON_CALL(store_, counter("test_stats.responses_validation_failed"))
        .WillByDefault(testing::ReturnRef(responses_validation_failed_));
    ON_CALL(store_, counter("test_stats.responses_validation_failed_enforced"))
        .WillByDefault(testing::ReturnRef(responses_validation_failed_enforced_));

    filter_config_ = std::make_unique<FilterConfig>("test_stats", scope_);
    filter_config_->processConfig(config);

    test_filter_ = std::make_unique<Filter>(*filter_config_ /*, stats*/);
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
  testing::NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  Stats::MockCounter requests_validated_;
  Stats::MockCounter requests_validation_failed_;
  Stats::MockCounter requests_validation_failed_enforced_;
  Stats::MockCounter responses_validated_;
  Stats::MockCounter responses_validation_failed_;
  Stats::MockCounter responses_validation_failed_enforced_;
};

class PayloadValidatorFilterTests : public PayloadValidatorFilterTestsBase,
                                    public ::testing::Test {
public:
    PayloadValidatorFilterTests() {
  test_request_headers_.setPath("/");
    }
protected:
  Http::TestRequestHeaderMapImpl test_request_headers_;
};

// Test configuration of requests
TEST_F(PayloadValidatorFilterTests, ValidateRequestMethod) {
  initialize(paths_header_config+root_path_config+post_method_config + get_method_config);

  // POST with subsequent body should be accepted (without calling sendLocalReply).
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Post);
  test_request_headers_.setPath("/");
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, false));

  // Header-only POST should be rejected. Body must be present.
  EXPECT_CALL(requests_validated_, inc());
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, true));

  // Header-only GET should be accepted
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_request_headers_, true));

  // PUT should not be accepted. Callback to send local reply should be called.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Put);
  EXPECT_CALL(requests_validated_, inc());
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::MethodNotAllowed, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, true));
}

TEST_F(PayloadValidatorFilterTests, ValidateRequestBody) {
  initialize(paths_header_config+root_path_config+post_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Post);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, false));

  std::string body = "{\"foo\": \"value\"}";
  Buffer::OwnedImpl data;

  data.add(body);

  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->decodeData(data, true));

  // Incorrect body - foo should be a string.
  body = "{\"foo\": 100}";
  data.drain(data.length());
  data.add(body);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, ValidateChunkedRequestBody) {
  initialize(paths_header_config+root_path_config+post_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Post);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, false));

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
  initialize(max_size_100_config+paths_header_config+root_path_config+post_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Post);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, false));

  // Body does not have to be json. It is rejected before passing to json parser.
  std::string body = "abcdefghji";
  Buffer::OwnedImpl data;

  // Create one chunk of data which exceeds max allowed size.
  for (auto i = 0; i < 11; i++) {
    data.add(body);
  }

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  // Should be rejected even when stream is not finished.
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, false));
  // should be rejected when stream is finished.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectTooLargeBodyChunked) {
  initialize(max_size_100_config+paths_header_config+root_path_config+post_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Post);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->decodeHeaders(test_request_headers_, false));

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
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, false));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectGetWithPayload) {
  initialize(max_size_0_config+paths_header_config+root_path_config+ get_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue, test_filter_->decodeHeaders(test_request_headers_, true));

  // Body does not have to be json. It is rejected before passing to json parser.
  std::string body = "a";
  Buffer::OwnedImpl data;
  data.add(body);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_CALL(requests_validation_failed_, inc());
  EXPECT_CALL(requests_validation_failed_enforced_, inc());
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->decodeData(data, true));
}

TEST_F(PayloadValidatorFilterTests, RejectInvalidHttpResponse) {
  initialize(paths_header_config+root_path_config+post_method_config + get_method_config);
  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers_, true));

  Http::TestResponseHeaderMapImpl test_response_headers;
  // Remove the status header.
  test_response_headers.removeStatus();

  EXPECT_CALL(responses_validated_, inc());
  EXPECT_CALL(responses_validation_failed_, inc());
  EXPECT_CALL(responses_validation_failed_enforced_, inc());
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_response_headers, true));
}

TEST_F(PayloadValidatorFilterTests, PassAllResponsesIfNothingConfigured) {
  initialize(paths_header_config+root_path_config+post_method_config + get_method_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers_, true));

  Http::TestResponseHeaderMapImpl test_response_headers;
  test_response_headers.setStatus("404");

  // Counter should not be updated when no response code is configured.
  EXPECT_CALL(responses_validated_, inc()).Times(0);
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->encodeHeaders(test_response_headers, true));
}

TEST_F(PayloadValidatorFilterTests, PassOnlyConfiguredResponses) {
  initialize(paths_header_config+root_path_config+post_method_config + get_method_config + empty_200_response_config);

  // Header decoding is necessary to select proper body validator.
  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers_, true));

  Http::TestResponseHeaderMapImpl test_response_headers;

  // 200 response is allowed, because it is configured.
  test_response_headers.setStatus("200");
  EXPECT_CALL(responses_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->encodeHeaders(test_response_headers, true));

  // 202 is not allowed.
  test_response_headers.setStatus("202");
  EXPECT_CALL(responses_validated_, inc());
  EXPECT_CALL(responses_validation_failed_, inc());
  EXPECT_CALL(responses_validation_failed_enforced_, inc());
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_response_headers, true));
}

TEST_F(PayloadValidatorFilterTests, CheckResponsePayload) {
  initialize(paths_header_config+root_path_config+post_method_config + get_method_config + empty_200_response_config +
             payload_for_200_response_config);

  test_request_headers_.setMethod(Http::Headers::get().MethodValues.Get);
  EXPECT_CALL(requests_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::Continue,
            test_filter_->decodeHeaders(test_request_headers_, true));

  Http::TestResponseHeaderMapImpl test_response_headers;

  // Send again headers, but without stream end.
  test_response_headers.setStatus("200");
  EXPECT_CALL(responses_validated_, inc());
  ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
            test_filter_->encodeHeaders(test_response_headers, false));

  // Send correct body.
  std::string body = "{\"foo\": \"value\"}";
  Buffer::OwnedImpl data;

  data.add(body);

  ASSERT_EQ(Http::FilterDataStatus::Continue, test_filter_->encodeData(data, true));

  // Send incorrect body.
  data.add("blahblah");
  EXPECT_CALL(responses_validation_failed_, inc());
  EXPECT_CALL(responses_validation_failed_enforced_, inc());
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::UnprocessableEntity, _, _, _, _));
  ASSERT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, test_filter_->encodeData(data, true));
}

const std::string parameters_header = R"EOF(
      parameters: 
  )EOF";

const std::string query_required_param1 = R"EOF(
      - name: "param1"
        in: QUERY
        required: true
        schema: |
          {
            "type": "string"
          }
  )EOF";

const std::string query_required_param2 = R"EOF(
      - name: "param2"
        in: QUERY
        required: true
        schema: |
          {
            "type": "string"
          }
  )EOF";

const std::string query_non_required_param3 = R"EOF(
      - name: "param3"
        in: QUERY
        required: false
        schema: |
          {
            "type": "integer"
          }
  )EOF";

using QueryParamsTestParam = std::tuple<std::vector<std::string>, std::string, bool>;
class QueryParamsPayloadValidatorFilterTest
    : public PayloadValidatorFilterTestsBase,
      // public ::testing::TestWithParam<std::tuple<std::vector<std::string>, std::string, bool>>
      // {};
      public ::testing::TestWithParam<QueryParamsTestParam> {};

const std::string test_path_config = R"EOF(
  - path: "/test"
    operations:
  )EOF";

TEST_P(QueryParamsPayloadValidatorFilterTest, QueryParamsTest) {
  std::string config = paths_header_config+test_path_config+post_method_config + get_method_config;

  if (!std::get<0>(GetParam()).empty()) {
    config += parameters_header;
  }
  for (const auto& param_config : std::get<0>(GetParam())) {
    config += param_config;
  }

  initialize(config);

  // Header decoding is necessary to select proper body validator.
  Http::TestRequestHeaderMapImpl test_request_headers;
  test_request_headers.setMethod(Http::Headers::get().MethodValues.Get);
  test_request_headers.setPath(std::get<1>(GetParam()));
  EXPECT_CALL(requests_validated_, inc());
  if (!std::get<2>(GetParam())) {
    EXPECT_CALL(requests_validation_failed_, inc());
    EXPECT_CALL(requests_validation_failed_enforced_, inc());
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
              test_filter_->decodeHeaders(test_request_headers, true));
  } else {
    ASSERT_EQ(Http::FilterHeadersStatus::Continue,
              test_filter_->decodeHeaders(test_request_headers, true));
  }
}

INSTANTIATE_TEST_SUITE_P(
    QueryParamsPayloadValidatorFilterTestsSuite, QueryParamsPayloadValidatorFilterTest,
    ::testing::Values(
        // Unexpected param.
        QueryParamsTestParam({}, "/test?param2=just_string", false),
        QueryParamsTestParam({query_required_param1}, "/test", false),
        QueryParamsTestParam({query_required_param1}, "/test?param1=just_string", true),
        // Repeated required parameter.
        QueryParamsTestParam({query_required_param1},
                             "/test?param1=just_string&param1=just_string", true),
        QueryParamsTestParam({query_required_param1},
                             "/test?param1=just_string&param3=just_string", false),

        // Required param2 is missing.
        QueryParamsTestParam({query_required_param1, query_required_param2},
                             "/test?param1=just_string", false),
        QueryParamsTestParam({query_required_param1, query_required_param2},
                             "/test?param1=just_string&param2=just_string", true),
        QueryParamsTestParam({query_required_param1, query_required_param2,
                              query_non_required_param3},
                             "/test?param1=just_string&param2=just_string&param3=101", true),
        // Optional param3 has wrong type.
        QueryParamsTestParam(
            {query_required_param1, query_required_param2, query_non_required_param3},
            "/test?param1=just_string&param2=just_string&param3=just_string", false)));


const std::string path_template_param1 = R"EOF(
      - name: "templated_segment_1"
        in: PATH
        schema: |
          {
            "type": "string"
          }
  )EOF";

const std::string path_template_param2 = R"EOF(
      - name: "templated_segment_2"
        in: PATH
        schema: |
          {
            "type": "integer",
            "minimum": 5,
            "maximum": 10
          }
  )EOF";

// Parameter types in the tuple:
// 0 - vector of config snippets which should be added to main config.
// 1 - path describing path matcher
// 2 - vector of pairs:
//     first item in pair is url which will be matched against path matcher
//     second item in the pair is expected result (true if path is accepted and false if rejected)
using PathsTestParam = std::tuple<std::vector<std::string>, std::string, std::vector<std::pair<std::string, bool>>>;
class PathsPayloadValidatorFilterTest
    : public PayloadValidatorFilterTestsBase,
      public ::testing::TestWithParam<PathsTestParam> {};

TEST_P(PathsPayloadValidatorFilterTest, PathsTest) {
  std::string path = fmt::format(R"EOF(
  - path: "{}"
    operations:
  )EOF", std::get<1>(GetParam()));
    
  std::string config = paths_header_config+path+get_method_config;

  // Add description of path parameters.
  if (!std::get<0>(GetParam()).empty()) {
    config += parameters_header;
  }
  for (const auto& param_config : std::get<0>(GetParam())) {
    config += param_config;
  }

  initialize(config);

  for (const auto& test : std::get<2>(GetParam())) {
  Http::TestRequestHeaderMapImpl test_request_headers;
  test_request_headers.setMethod(Http::Headers::get().MethodValues.Get);
  test_request_headers.setPath(test.first);
  EXPECT_CALL(requests_validated_, inc());
  if (!test.second) {
    EXPECT_CALL(requests_validation_failed_, inc());
    EXPECT_CALL(requests_validation_failed_enforced_, inc());
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration,
              test_filter_->decodeHeaders(test_request_headers, true));
  } else {
    ASSERT_EQ(Http::FilterHeadersStatus::Continue,
              test_filter_->decodeHeaders(test_request_headers, true));
  }
    }
}

INSTANTIATE_TEST_SUITE_P(
    PathsPayloadValidatorFilterTestsSuite, PathsPayloadValidatorFilterTest,
    ::testing::Values(
        PathsTestParam({}, "/segment1", {std::make_pair<std::string, bool>("/segment1", true),
                                         std::make_pair<std::string, bool>("/segment1/segment2", false),
                                         std::make_pair<std::string, bool>("/segment2", false)}),
        PathsTestParam({}, "/segment1/segment2", {std::make_pair<std::string, bool>("/segment1", false),
                                         std::make_pair<std::string, bool>("/segment2", false),
                                         std::make_pair<std::string, bool>("/segment1/segment2/segment3", false),
                                         std::make_pair<std::string, bool>("/segment1/segment2", true)}),
        PathsTestParam({path_template_param1}, "/{templated_segment_1}", {std::make_pair<std::string, bool>("/segment1", true),
                                         std::make_pair<std::string, bool>("/100", true),
                                         std::make_pair<std::string, bool>("/segment1/segment2", false),
                                         std::make_pair<std::string, bool>("/segment2", true)}),
        PathsTestParam({path_template_param1}, "/segment1/{templated_segment_1}", {std::make_pair<std::string, bool>("/segment1/segment2", true),
                                         std::make_pair<std::string, bool>("/segment1/100", true),
                                         std::make_pair<std::string, bool>("/segment1/segment2/segment3", false),
                                         std::make_pair<std::string, bool>("/segment2", false)}),
        PathsTestParam({path_template_param1, path_template_param2}, "/segment1/{templated_segment_1}/{templated_segment_2}", 
                                         {std::make_pair<std::string, bool>("/segment1/segment2", false),
                                         std::make_pair<std::string, bool>("/segment1/100/8", true),
                                         std::make_pair<std::string, bool>("/segment1/segment2/11", false),
                                         std::make_pair<std::string, bool>("/segment1/segment2/segment3", false),
                                         std::make_pair<std::string, bool>("/segment2", false)})
        ));

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
