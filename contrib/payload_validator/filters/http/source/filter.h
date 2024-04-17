#pragma once

#include <cstdint>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

//#include "source/common/json/json_loader.h"

//#include "contrib/dynamo/filters/http/source/dynamo_request_parser.h"
//#include "contrib/dynamo/filters/http/source/dynamo_stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PayloadValidator {

class PayloadValidatorStats {
public:
  PayloadValidatorStats() {}
};

/**
 * DynamoDb filter to process egress request to dynamo and capture comprehensive stats
 * It captures RPS/latencies:
 *  1) Per table per response code (and group of response codes, e.g., 2xx/3xx/etc)
 *  2) Per operation per response code (and group of response codes, e.g., 2xx/3xx/etc)
 */
class Filter : public Http::StreamFilter {
public:
  Filter(const std::shared_ptr<PayloadValidatorStats>&) {}
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

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace PayloadValidator
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
