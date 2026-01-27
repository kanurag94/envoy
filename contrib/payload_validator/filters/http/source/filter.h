#pragma once

#include <cstdint>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"

#include "contrib/payload_validator/filters/http/source/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

/**
 */
class Filter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::payload_validator> {
public:
  Filter(FilterConfig& config) : config_(config) {}
  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  std::shared_ptr<PayloadValidatorStats> stats() const { return config_.stats(); }

private:
  bool onRequestValidationFailure(absl::string_view, Http::Code);
  bool onResponseValidationFailure(absl::string_view, Http::Code);

  FilterConfig& config_;

  std::shared_ptr<Operation> current_operation_;
  std::shared_ptr<JSONBodyValidator> response_validator_;
  bool local_reply_{false};

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};

  // This value determines if validation should be performed.
  // It is flipped to false after first validation error is detected
  // when running in non-enforcing mode.
  bool validate_{true};

  std::string formatErrorMessageForResponseCodeDetails(absl::string_view error_message);
};

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
