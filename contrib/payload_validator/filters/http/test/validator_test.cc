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

class TestJSONValidator : public JSONValidator {
public:
  bool initialize(const std::string& schema) override { return initializeValidator(schema); }
  std::pair<bool, absl::optional<std::string>> validate(absl::string_view) override {
  return std::make_pair<bool, absl::optional<std::string>>(true, std::nullopt); }
    
};

TEST(JSONValidator, SchemaCheck) {
    
     TestJSONValidator validator;

    // Non-JSON schema.
    ASSERT_FALSE(validator.initialize("blah"));

    //ASSERT_FALSE(validator.initialize("{\"blah\": \"blah\"}"));
}

TEST(ParamValidatorBase, SchemaCheck) {
  ParamValidatorBase validator("blahblah");

  // Non-JSON schema.
  ASSERT_FALSE(validator.initialize("wrong schema"));
}

// This test only validates whether validator accepts the schema
// and is able to accept or reject simple parameter values.
// It does not test more complex schemas like max, min for integer
// or max, min string length as those tests should be part of
// validation library.
TEST(ParamValidatorBase, ValidationCheck) {
  ParamValidatorBase validator("blahblah");

  // "blahblah" param should be integer.
  ASSERT_TRUE(validator.initialize("{\"type\": \"integer\"}"));

  ASSERT_TRUE(validator.validate("100").first);
  // The same value but as a string
  ASSERT_FALSE(validator.validate("\"100\"").first);
}

TEST(QueryParamValidator, ValidateQueryParams) {
  absl::flat_hash_map<std::string, std::unique_ptr<QueryParamValidator>> params;

  ASSERT_TRUE(validateParams(params, "/test").first);

  // Unexpected param.
  ASSERT_FALSE(validateParams(params, "/test?param1=test").first);

  // Add one non-required parameter to list of validators.
  std::unique_ptr<QueryParamValidator> param_validator = std::make_unique<QueryParamValidator>("param1");
  param_validator->initialize("{\"type\": \"string\"}");
  param_validator->required(false);
  params.emplace("param1", std::move(param_validator));

  // Still should be OK, because param1 is not required.
  ASSERT_TRUE(validateParams(params, "/test").first);
  ASSERT_TRUE(validateParams(params, "/test?param1=test").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=test").first);
  // Unexpected characters in param's value.
  ASSERT_FALSE(validateParams(params, "/test?param1=\"test\"").first);

  // Add one required parameter to the list of validators.
  param_validator = std::make_unique<QueryParamValidator>("param2");
  param_validator->initialize("{\"type\": \"string\"}");
  ASSERT_TRUE(param_validator->required());
  params.emplace("param2", std::move(param_validator));

  // Required param2 is missing.
  ASSERT_FALSE(validateParams(params, "/test").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=test").first);
  ASSERT_TRUE(validateParams(params, "/test?param2=test").first);
  ASSERT_TRUE(validateParams(params, "/test?param1=test&param2=test").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=test&param3=test").first);

  // Add the second required parameter to the list of validators.
  param_validator = std::make_unique<QueryParamValidator>("param3");
  param_validator->initialize("{\"type\": \"string\"}");
  ASSERT_TRUE(param_validator->required());
  params.emplace("param3", std::move(param_validator));

  ASSERT_FALSE(validateParams(params, "/test").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=test").first);
  ASSERT_FALSE(validateParams(params, "/test?param2=test").first);
  ASSERT_FALSE(validateParams(params, "/test?param1=test&param2=test").first);
  ASSERT_TRUE(validateParams(params, "/test?param2=test&param3=test").first);
  // Repeat the same required parameter. It should still detect that param3 is missing.
  ASSERT_FALSE(validateParams(params, "/test?param2=test&param2=test").first);
}

TEST(FixedPathSegmentValidator, SingleSegmentComparison) {
   FixedPathSegmentValidator v("SegmeNt1", 3); 

   ASSERT_TRUE(v.validate("segment1"));   
   ASSERT_TRUE(v.validate("SeGmEnT1"));   
   ASSERT_FALSE(v.validate("segment2"));   
   ASSERT_FALSE(v.validate("SeGmEnT2"));   
   EXPECT_EQ(v.segmentNumber(), 3);
}

TEST(FixedPathSegmentValidator, AllFixedPathsComparison) {
    PathTemplateValidator path_template;
    path_template.fixed_segments_.emplace_back("segment1", 0);
    path_template.fixed_segments_.emplace_back("segment2", 1);

    ASSERT_THAT(checkPath(path_template, {"segment1", "segment2"}).first, PathValidationResult::MATCHED);
    ASSERT_THAT(checkPath(path_template, {"segment2", "segment1"}).first, PathValidationResult::NOT_MATCHED);
    ASSERT_THAT(checkPath(path_template, {"segment1", "segment1"}).first, PathValidationResult::NOT_MATCHED);
    ASSERT_THAT(checkPath(path_template, {"segment2", "segment2"}).first, PathValidationResult::NOT_MATCHED);
    ASSERT_THAT(checkPath(path_template, {"segment1", "segment3"}).first, PathValidationResult::NOT_MATCHED);
    ASSERT_THAT(checkPath(path_template, {"segment3", "segment2"}).first, PathValidationResult::NOT_MATCHED);

#if 0
    // Matching multiple templates should be moved to filter tests.
    ASSERT_FALSE(validatePath(allowed_paths, "/segment2").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/").first);

    path_template = std::make_unique<PathTemplateValidator>();
    path_template->fixed_segments_.emplace_back("segment1", 0);
    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({1, std::move(path_templates)});

    ASSERT_TRUE(validatePath(allowed_paths, "/segment1/segment2").first);
    ASSERT_TRUE(validatePath(allowed_paths, "/segment1").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment2").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/").first);

    // Add empty path. 
    path_template = std::make_unique<PathTemplateValidator>();
    path_template->fixed_segments_.emplace_back("", 0);
    allowed_paths[1].push_back(std::move(path_template));

    ASSERT_TRUE(validatePath(allowed_paths, "/").first);
    // Those paths should still be accepted or denied as before.
    ASSERT_TRUE(validatePath(allowed_paths, "/segment1/segment2").first);
    ASSERT_TRUE(validatePath(allowed_paths, "/segment1").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment2").first);

    
    // Remove "/segment1/segment2" path.
    allowed_paths.erase(2);
    ASSERT_TRUE(validatePath(allowed_paths, "/").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment1/segment2").first);
    ASSERT_TRUE(validatePath(allowed_paths, "/segment1").first);
#endif
}

TEST(TemplatedPathSegmentValidation, AllTemplatedPathsComparison) {
    AllowedPaths allowed_paths;

    std::vector<std::unique_ptr<PathTemplateValidator>> path_templates;

    PathTemplateValidator path_template;
    path_template.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment1", 0));
    // segment1 should be integer.
    ASSERT_TRUE(path_template.templated_segments_.back()->initialize("{\"type\": \"integer\"}"));

    ASSERT_THAT(checkPath(path_template, {"123"}).first, PathValidationResult::MATCHED);
    ASSERT_THAT(checkPath(path_template, {"\"segment1\""}).first, PathValidationResult::MATCHED_WITH_ERRORS);


    path_template.templated_segments_.clear();
    // Add another template to match string to match /{integer}/{string}
    path_template.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment2", 0));
    // segment2 should be integer.
    ASSERT_TRUE(path_template.templated_segments_.back()->initialize("{\"type\": \"integer\"}"));
    path_template.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment3", 1));
    // segment3 should be string.
    ASSERT_TRUE(path_template.templated_segments_.back()->initialize("{\"type\": \"string\"}"));

    // Correct format. integer and string.
    ASSERT_THAT(checkPath(path_template, {"123", "test"}).first, PathValidationResult::MATCHED);
    // Incorrect format. string and string.
    ASSERT_THAT(checkPath(path_template, {"part1", "part2"}).first, PathValidationResult::MATCHED_WITH_ERRORS);
}

TEST(TemplatedPathSegmentValidation, MixedPathsComparison) {
    // Construct metchers for partially templated path /{segment1}/segment2/segment3/{segment4}
    // where segment1 should be a string and segment4 should be integer.
    PathTemplateValidator path_template;
    path_template.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment1", 0));
    // segment1 should be integer.
    ASSERT_TRUE(path_template.templated_segments_.back()->initialize("{\"type\": \"string\"}"));

    path_template.templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment4", 3));
    // segment3 should be string.
    ASSERT_TRUE(path_template.templated_segments_.back()->initialize("{\"type\": \"integer\"}"));

    // Fixed segments.
    path_template.fixed_segments_.emplace_back("segment2", 1);
    path_template.fixed_segments_.emplace_back("segment3", 2);

    ASSERT_THAT(checkPath(path_template, {"segment1", "segment2", "segment3", "123"}).first, PathValidationResult::MATCHED);
    ASSERT_THAT(checkPath(path_template,{ "segment1", "segment2", "segment3", "segment4"}).first, PathValidationResult::MATCHED_WITH_ERRORS);
    ASSERT_THAT(checkPath(path_template, {"123","segment2", "segment3","segment4"}).first, PathValidationResult::MATCHED_WITH_ERRORS);
    ASSERT_THAT(checkPath(path_template, {"segment1", "segment2", "segment33", "123"}).first, PathValidationResult::NOT_MATCHED);
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
