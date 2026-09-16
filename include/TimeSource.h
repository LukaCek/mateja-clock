#pragma once

#include <cstdint>

// Pure, Arduino-free helper that selects the best available time source
// (NTP system clock vs. DS1302 RTC coprocessor). Testable on native.
namespace timesource {

enum class Source { kInvalid, kNtp, kRtc };

// Evaluate the authoritative time source from the given inputs.
// systemNow: the host's `time(nullptr)` (or a synthetic clock in tests).
// floor:     minimum valid epoch (same constant used by TimeService/RtcLinkCore).
// rtcValid:  true when the last DS1302 sample is trusted.
// rtcEpoch:  the UTC epoch from the DS1302 (only used when rtcValid).
Source select(std::int64_t systemNow, std::int64_t floor, bool rtcValid,
             std::int64_t rtcEpoch);

// Return the authoritative epoch in seconds for the selected source.
std::int64_t resolveEpoch(Source source, std::int64_t systemNow,
                          std::int64_t rtcEpoch);

}  // namespace timesource