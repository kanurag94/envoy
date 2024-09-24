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

TEST(FixedPathSegmentValidator, SingleSegmentComparison) {
   FixedPathSegmentValidator v("SegmeNt1", 3); 

   ASSERT_TRUE(v.validate("segment1"));   
   ASSERT_TRUE(v.validate("SeGmEnT1"));   
   ASSERT_FALSE(v.validate("segment2"));   
   ASSERT_FALSE(v.validate("SeGmEnT2"));   
   EXPECT_EQ(v.segmentNumber(), 3);
}

TEST(FixedPathSegmentValidator, AllFixedPathsComparison) {
    AllowedPaths allowed_paths;

    // Path template contains only fixed segments, not templated.
    std::vector<FixedPathSegmentValidator> fixed_segments;

    // TODO: add a function which takes a templatized path and creates this config.
    // Then add unit test to verify the whole structure layout.

    std::vector<std::unique_ptr<PathTemplate>> path_templates;
    
    std::unique_ptr<PathTemplate> path_template = std::make_unique<PathTemplate>();
    path_template->fixed_segments_.emplace_back("segment1", 0);
    path_template->fixed_segments_.emplace_back("segment2", 1);
    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({2, std::move(path_templates)});

    ASSERT_TRUE(validatePath(allowed_paths, "/segment1/segment2").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment1").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment2").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/").first);

    path_template = std::make_unique<PathTemplate>();
    path_template->fixed_segments_.emplace_back("segment1", 0);
    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({1, std::move(path_templates)});

    ASSERT_TRUE(validatePath(allowed_paths, "/segment1/segment2").first);
    ASSERT_TRUE(validatePath(allowed_paths, "/segment1").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/segment2").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/").first);

    // Add empty path. 
    path_template = std::make_unique<PathTemplate>();
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
}

TEST(TemplatedPathSegmentValidation, AllTemplatedPathsComparison) {
    AllowedPaths allowed_paths;

    // Path contains only templated segments.
    std::vector<TemplatedPathSegmentValidator> templated_segments;

    // TODO: add a function which takes a templatized path and creates this config.
    std::vector<std::unique_ptr<PathTemplate>> path_templates;

    std::unique_ptr<PathTemplate> path_template = std::make_unique<PathTemplate>();
    path_template->templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment1", 0));
    // segment1 should be integer.
    ASSERT_TRUE(path_template->templated_segments_.back()->initialize("{\"type\": \"integer\"}"));
    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({1, std::move(path_templates)});

    ASSERT_TRUE(validatePath(allowed_paths, "/123").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/\"segment1\"").first);

    // Add another template to match string to match /{integer}/{string}
    path_template = std::make_unique<PathTemplate>();
    path_template->templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment2", 0));
    // segment2 should be integer.
    ASSERT_TRUE(path_template->templated_segments_.back()->initialize("{\"type\": \"integer\"}"));
    path_template->templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment3", 1));
    // segment3 should be string.
    ASSERT_TRUE(path_template->templated_segments_.back()->initialize("{\"type\": \"string\"}"));
    
    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({2, std::move(path_templates)});

    // Correct format. integer and string.
    ASSERT_TRUE(validatePath(allowed_paths, "/123/\"test\"").first);
    // Incorrect format. string and string.
    ASSERT_FALSE(validatePath(allowed_paths, "/\"part1\"/\"part2\"").first);
    // Incorrect path. 3 segments.
    ASSERT_FALSE(validatePath(allowed_paths, "/\"part1\"/\"part2\"/\"part3\"").first);
}

TEST(TemplatedPathSegmentValidation, MixedPathsComparison) {
    AllowedPaths allowed_paths;

    // Path contains only templated segments.
    std::vector<TemplatedPathSegmentValidator> templated_segments;

    // TODO: add a function which takes a templatized path and creates this config.
    std::vector<std::unique_ptr<PathTemplate>> path_templates;

    // Construct metchers for partially templated path /{segment1}/segment2/segment3/{segment4}
    // where segment1 should be a string and segment4 should be integer.
    std::unique_ptr<PathTemplate> path_template = std::make_unique<PathTemplate>();
    path_template->templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment1", 0));
    // segment1 should be integer.
    ASSERT_TRUE(path_template->templated_segments_.back()->initialize("{\"type\": \"string\"}"));

    path_template->templated_segments_.push_back(std::make_unique<TemplatedPathSegmentValidator>("segment4", 3));
    // segment3 should be string.
    ASSERT_TRUE(path_template->templated_segments_.back()->initialize("{\"type\": \"integer\"}"));

    // Fixed segments.
    path_template->fixed_segments_.emplace_back("segment2", 1);
    path_template->fixed_segments_.emplace_back("segment3", 2);

    path_templates.push_back(std::move(path_template));
    allowed_paths.insert({4, std::move(path_templates)});

    ASSERT_FALSE(validatePath(allowed_paths, "/123").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/\"segment1\"").first);
    ASSERT_TRUE(validatePath(allowed_paths, "/\"segment1\"/segment2/segment3/123").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/\"segment1\"/segment2/segment3/\"segment4\"").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/123/segment2/segment3/\"segment4\"").first);
    ASSERT_FALSE(validatePath(allowed_paths, "/\"segment1\"/segment2/segment33/123").first);
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
