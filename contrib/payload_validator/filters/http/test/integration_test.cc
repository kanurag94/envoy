#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {
// namespace {

class PayloadValidatorIntegrationTest
    : public Envoy::HttpIntegrationTest,
      public ::testing::TestWithParam<std::tuple<std::string, bool, std::string, std::string>> {
public:
  PayloadValidatorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {

    std::string filter_config = R"EOF(
name: envoy.filters.http.payload_validator
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.payload_validator.v3.PayloadValidator
  stat_prefix: test_p_v
  paths:
  - path: "/"
    operations:
    - method: POST  
      request_max_size: 25
      request_body:
        schema: |
          {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "title": "A person",
              "properties": {
                  "foo": {
                      "type": "string",
                      "minLength": 10,
                      "maxLength": 10
                  }
              },
              "required": [
                  "foo"
              ],
              "type": "object"
          }
    - method: DELETE
    - method: PUT
      request_max_size: 0
)EOF";
    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

TEST_P(PayloadValidatorIntegrationTest, RejectedRequests) {
  const auto& param = GetParam();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", std::get<0>(param)},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};

  IntegrationStreamDecoderPtr response;
  if (std::get<1>(param)) {
    // Send body.
    response = codec_client_->makeRequestWithBody(request_headers, std::get<3>(param));
  } else {
    response = codec_client_->makeHeaderOnlyRequest(request_headers);
  }

  // If expected reply code is 200, it means that test assumes that payload
  // was successfully validated and upstream server whould return 200.
  if (std::get<2>(param) == "200") {
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  }
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs(std::get<2>(param)));

  test_server_->waitForCounterEq("http.config_test.payload_validator.test_p_v.requests_validated",
                                 1);
  if (std::get<2>(param) != "200") {
    EXPECT_EQ(
        1, test_server_
               ->counter("http.config_test.payload_validator.test_p_v.requests_validation_failed")
               ->value());
    EXPECT_EQ(
        1,
        test_server_
            ->counter(
                "http.config_test.payload_validator.test_p_v.requests_validation_failed_enforced")
            ->value());
  }
}

// The following test cases test payload validation of requests.
// Test cases target different logical paths within the filter,
// not the payload validating library (one test case with wrong body
// is enough to determine that payload validator was reached).
INSTANTIATE_TEST_SUITE_P(
    PayloadValidatorIntegrationTestSuite, PayloadValidatorIntegrationTest,
    ::testing::Values(
        // POST without body.
        std::make_tuple("POST", false, "422", "{}"),
        // POST with correct body.
        std::make_tuple("POST", true, "200", "{\"foo\":\"abcdefghij\"}"),
        // POST with incorrect body.
        std::make_tuple("POST", true, "422", "{\"foo\": 1}"),
        // POST with too large body. Body length is checked before passing it to validator.
        std::make_tuple("POST", true, "413", "{\"foo\":\"abcdefghijklmnop\"}"),
        // DELETE is allowed but body is not validated. With or without body it should not be
        // stopped.
        std::make_tuple("DELETE", true, "200", "{\"foo\":\"abcdefghijklmnop\"}"),
        std::make_tuple("DELETE", false, "200", ""),
        // PUT's body must not be present. Max allowed body length is zero.
        std::make_tuple("PUT", true, "413", "{\"foo\":\"abcdefghijklmnop\"}"),
        std::make_tuple("PUT", false, "200", ""),
        // GET is not allowed.
        std::make_tuple("GET", false, "405", "")));

// Validate responses.
class ResponseValidatorIntegrationTest
    : public Envoy::HttpIntegrationTest,
      public ::testing::TestWithParam<
          std::tuple<std::string, std::string, bool, std::string, std::string>> {
public:
  ResponseValidatorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {

    std::string filter_config = R"EOF(
name: envoy.filters.http.payload_validator
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.payload_validator.v3.PayloadValidator
  stat_prefix: test_p_v
  paths:
  - path: "/"
    operations:
    - method: GET  
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
    - method: DELETE
    - method: PUT
      request_max_size: 0
)EOF";
    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

TEST_P(ResponseValidatorIntegrationTest, RejectedRequests) {
  const auto& param = GetParam();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", std::get<0>(param)},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};

  IntegrationStreamDecoderPtr response;
  response = codec_client_->makeHeaderOnlyRequest(request_headers);

  waitForNextUpstreamRequest();

  if (std::get<2>(GetParam())) {
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", std::get<1>(param)}}, false);
    upstream_request_->encodeData(std::get<4>(param), true);
  } else {
    // Send only headers
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", std::get<1>(param)}}, true);
  }
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs(std::get<3>(param)));

  test_server_->waitForCounterEq("http.config_test.payload_validator.test_p_v.responses_validated",
                                 1);
  if (response->headers().getStatusValue() != "200") {
    EXPECT_EQ(
        1, test_server_
               ->counter("http.config_test.payload_validator.test_p_v.responses_validation_failed")
               ->value());
    EXPECT_EQ(
        1,
        test_server_
            ->counter(
                "http.config_test.payload_validator.test_p_v.responses_validation_failed_enforced")
            ->value());
  }
}

// The following test cases test payload validation of requests.
// Test cases target different logical paths within the filter,
// not the payload validating library (one test case with wrong body
// is enough to determine that payload validator was reached).
INSTANTIATE_TEST_SUITE_P(ResponseValidatorIntegrationTestSuite, ResponseValidatorIntegrationTest,
                         ::testing::Values(
                             // Response to GET without body.
                             std::make_tuple("GET", "200", false, "422", "{}"),
                             // Response to GET with incorrect body, but still in json format.
                             std::make_tuple("GET", "200", true, "422", "{\"foo\": 1}"),
                             // Response to GET with incorrect body.
                             std::make_tuple("GET", "200", true, "422", "blah}"),
                             // Response to GET with not allowed response code 202.
                             std::make_tuple("GET", "202", true, "422", ""),
                             // Response to GET with correct body.
                             std::make_tuple("GET", "200", true, "200",
                                             "{\"foo\":\"abcdefghij\"}")));

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
