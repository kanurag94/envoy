#include "contrib/payload_validator/filters/http/source/filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/http/codes.h"
#include "source/common/http/exception.h"
#include "source/common/http/utility.h"

//#include "source/common/json/json_loader.h"
#include <nlohmann/json-schema.hpp>

#include "absl/container/fixed_array.h"

using nlohmann::json;
using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // get method header
  const absl::string_view method = headers.getMethodValue();
  const auto& it = config_.operations_.find(method);

  if (it == config_.operations_.end()) {
    // Return method not allowed.
    decoder_callbacks_->sendLocalReply(Http::Code::MethodNotAllowed, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Store the pointer to the description of request and response associated with the received
  // method.
  current_operation_ = (*it).second;

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool stream_end) {

  // If there is a request validator for this method, entire data must be buffered
  // in order to do validation.
  // If there is no validator, there is no need for buffering.

  auto& req_validator = current_operation_->request_;
  if (req_validator == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  if (!stream_end) {
    // TODO: check the size of data.
    decoder_callbacks_->addDecodedData(data, true);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  const auto* buffer = decoder_callbacks_->decodingBuffer();
  if (buffer == nullptr) {
    buffer = &data;
  }
  if (buffer->length() != 0) {
    auto result = req_validator->validate(*buffer);

    if (!result.first) {
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
                                         std::string("Request validation failed: ") +
                                             result.second.value(),
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {

  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
