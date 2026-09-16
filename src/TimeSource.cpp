#include "TimeSource.h"

namespace timesource {

Source select(std::int64_t systemNow, std::int64_t floor, bool rtcValid,
             std::int64_t rtcEpoch) {
  if (systemNow >= floor) {
    return Source::kNtp;
  }
  if (rtcValid && rtcEpoch >= floor) {
    return Source::kRtc;
  }
  return Source::kInvalid;
}

std::int64_t resolveEpoch(Source source, std::int64_t systemNow,
                          std::int64_t rtcEpoch) {
  switch (source) {
    case Source::kNtp:
      return systemNow;
    case Source::kRtc:
      return rtcEpoch;
    case Source::kInvalid:
      return 0;
  }
  return 0;
}

}  // namespace timesource