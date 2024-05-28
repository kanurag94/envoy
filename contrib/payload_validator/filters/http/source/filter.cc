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

  if (config_.operations_.find(method) == config_.operations_.end()) {
    // Return method not allowed.
    decoder_callbacks_->sendLocalReply(Http::Code::MethodNotAllowed, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool /*stream_end*/) {

  if (data.length() != 0) {
    auto v = config_.operations_.find("GET");
    auto& req_validator = (*v).second->request_;
    auto result = req_validator->validate(data);

    if (!result.first) {
      decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity, result.second.value(),
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
#if 0    
    // Get access to data.
    std::string message;
    message.assign(std::string(static_cast<char*>(data.linearize(data.length())), data.length()));

    std::cerr << "Calling with end_stream: " << stream_end << "\n";
    // Todo (reject if this is not json).
    json rec_buf = json::parse(message);
    
    try {
    config_.getValidator().validate(rec_buf);
    } catch (const std::exception &e) {
    std::cerr << "Payload does not match the schema, here is why: " << e.what() << "\n";

    decoder_callbacks_->sendLocalReply(Http::Code::UnprocessableEntity, e.what(),
                             nullptr, absl::nullopt, "");
    return Http::FilterDataStatus::StopIterationNoBuffer;
    }
#endif
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
