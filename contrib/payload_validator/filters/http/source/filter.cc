#include "contrib/payload_validator/filters/http/source/filter.h"

#include <chrono>
#include <cstdint>
#include <nlohmann/json-schema.hpp>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/http/codes.h"
#include "source/common/http/exception.h"
#include "source/common/http/utility.h"

#include "absl/container/fixed_array.h"

using nlohmann::json;
using nlohmann::json_schema::json_validator;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool stream_end) {
  // This is the beginning of processing of payloads.
  config_.stats()->requests_validated_.inc();
  // get method header
  const absl::string_view method = headers.getMethodValue();
  const auto& it = config_.operations_.find(method);
  local_reply_ = false;

  if (it == config_.operations_.end()) {
    // Return method not allowed.
    local_reply_ = true;
    decoder_callbacks_->sendLocalReply(Http::Code::MethodNotAllowed, "", nullptr, absl::nullopt,
                                       "");
    config_.stats()->requests_validation_failed_.inc();
    config_.stats()->requests_validation_failed_enforced_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Store the pointer to the description of request and response associated with the received
  // method.
  current_operation_ = (*it).second;

  if (stream_end) {
    if (current_operation_->request_->active()) {
      local_reply_ = true;
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity, "Payload body is missing",
                                         nullptr, absl::nullopt, "");
      config_.stats()->requests_validation_failed_.inc();
      config_.stats()->requests_validation_failed_enforced_.inc();
      return Http::FilterHeadersStatus::StopIteration;
    };
    return Http::FilterHeadersStatus::Continue;
  }

  // Do not send headers upstream yet, because the body validation may fail.
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool stream_end) {
  // If there is a request validator for this method, entire data must be buffered
  // in order to do validation.
  // If there is no validator, there is no need for buffering.

  auto& req_validator = current_operation_->request_;
  if (req_validator == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  const auto* buffer = decoder_callbacks_->decodingBuffer();

  uint32_t total_length = data.length();
  if (buffer != nullptr) {
    total_length += buffer->length();
  }

  if (total_length > req_validator->maxSize()) {
    local_reply_ = true;
    decoder_callbacks_->sendLocalReply(
        Http::Code::PayloadTooLarge,
        fmt::format("Request validation failed. Payload exceeds {} bytes",
                    req_validator->maxSize()),
        nullptr, absl::nullopt, "");
    config_.stats()->requests_validation_failed_.inc();
    config_.stats()->requests_validation_failed_enforced_.inc();
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (!stream_end) {
    decoder_callbacks_->addDecodedData(data, false);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  if (!req_validator->active()) {
    return Http::FilterDataStatus::Continue;
  }

  if (buffer == nullptr) {
    buffer = &data;
  } else {
    decoder_callbacks_->addDecodedData(data, false);
  }
  if (buffer->length() != 0) {
    auto result = req_validator->validate(*buffer);

    if (!result.first) {
      local_reply_ = true;
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
                                         std::string("Request validation failed: ") +
                                             result.second.value(),
                                         nullptr, absl::nullopt, "");
      config_.stats()->requests_validation_failed_.inc();
      config_.stats()->requests_validation_failed_enforced_.inc();
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {

  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool stream_end) {
  if (local_reply_) {
    return Http::FilterHeadersStatus::Continue;
  }
  // get Status header
  absl::optional<uint64_t> status = Http::Utility::getResponseStatusOrNullopt(headers);

  if (status == absl::nullopt) {
    local_reply_ = true;
    encoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
                                       "Incorrect response. Status header is missing.", nullptr,
                                       absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (current_operation_->responses_.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto& it = current_operation_->responses_.find(status.value());

  if (it == current_operation_->responses_.end()) {
    local_reply_ = true;
    // Return method not allowed.
    encoder_callbacks_->sendLocalReply(
        Http::Code::UnprocessableEntity,
        fmt::format("Not allowed response status code: {}", status.value()), nullptr, absl::nullopt,
        "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (stream_end) {
    if ((*it).second != nullptr) {
      // Body is not present but is required.
      local_reply_ = true;
      // Return method not allowed.
      encoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
                                         "Response body is missing", nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    } else {
      return Http::FilterHeadersStatus::Continue;
    }
  }

  // Store reference to response validator.
  response_validator_ = (*it).second;

  // Do not continue yet, as body validation may fail.
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool stream_end) {
  if (local_reply_) {
    return Http::FilterDataStatus::Continue;
  }

  if (response_validator_ == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  const auto* buffer = encoder_callbacks_->encodingBuffer();

  if (!stream_end) {
    encoder_callbacks_->addEncodedData(data, false);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  if (buffer == nullptr) {
    buffer = &data;
  } else {
    encoder_callbacks_->addEncodedData(data, false);
  }
  if (buffer->length() != 0) {
    auto result = response_validator_->validate(*buffer);

    if (!result.first) {
      local_reply_ = true;
      encoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity,
                                         std::string("Request validation failed: ") +
                                             result.second.value(),
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
