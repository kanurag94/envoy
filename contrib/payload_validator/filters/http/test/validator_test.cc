#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/payload_validator/filters/http/source/validator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

TEST(ParamValidator, SchemaCheck) {
  ParamValidator validator("blahblah");

  // Non-JSON schema.
  ASSERT_FALSE(validator.initialize("wrong schema"));
}

// This test only validates whether validator accepts the schema
// and is able to accept or reject simple parameter values.
// It does not test more complex schemas like max, min for integer
// or max, min string length as those tests should be part of
// validation library.
TEST(ParamValidator, ValidationCheck) {
  ParamValidator validator("blahblah");

  // "blahblah" param should be integer.
  ASSERT_TRUE(validator.initialize("{\"type\": \"integer\"}"));

  ASSERT_TRUE(validator.validate("100").first);
  // The same value but as a string
  ASSERT_FALSE(validator.validate("\"100\"").first);

  // "blahblah" param should be string.
  ASSERT_TRUE(validator.initialize("{\"type\": \"string\"}"));

  ASSERT_FALSE(validator.validate("100").first);
  // The same value but as a string
  ASSERT_TRUE(validator.validate("\"100\"").first);
}

TEST(ParamValidator, ValidateQueryParams) {
  absl::flat_hash_map<std::string, std::unique_ptr<ParamValidator>> params;

  ASSERT_TRUE(validateParams(params, "/test").first);

  // Unexpected param.
  ASSERT_FALSE(validateParams(params, "/test?param1=test").first);

  // Add one non-required parameter to list of validators.
  std::unique_ptr<ParamValidator> param_validator = std::make_unique<ParamValidator>("param1");
  param_validator->initialize("{\"type\": \"string\"}");
  param_validator->required(false);
  params.emplace("param1", std::move(param_validator));

  // Still should be OK, because param1 is not required.
  ASSERT_TRUE(validateParams(params, "/test").first);
  ASSERT_TRUE(validateParams(params, "/test?param1=\"test\"").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=\"test\"").first);

  // Add one required parameter to the list of validators.
  param_validator = std::make_unique<ParamValidator>("param2");
  param_validator->initialize("{\"type\": \"string\"}");
  ASSERT_TRUE(param_validator->required());
  params.emplace("param2", std::move(param_validator));

  // Required param2 is missing.
  ASSERT_FALSE(validateParams(params, "/test").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=\"test\"").first);
  ASSERT_TRUE(validateParams(params, "/test?param2=\"test\"").first);
  ASSERT_TRUE(validateParams(params, "/test?param1=\"test\"&param2=\"test\"").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=\"test\"&param3=\"test\"").first);

  // Add the second required parameter to the list of validators.
  param_validator = std::make_unique<ParamValidator>("param3");
  param_validator->initialize("{\"type\": \"string\"}");
  ASSERT_TRUE(param_validator->required());
  params.emplace("param3", std::move(param_validator));

  ASSERT_FALSE(validateParams(params, "/test").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=\"test\"").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=\"test\"").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=\"test\"&param2=\"test\"").first);
  ASSERT_TRUE(validateParams(params, "/test?param2=\"test\"&param3=\"test\"").first);
  // Repeat the same required parameter. It should still detect that param3 is missing.
  ASSERT_FALSE(validateParams(params, "/test?param2=\"test\"&param2=\"test\"").first);
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
