#pragma once

#include <cstddef>
#include <cstdint>

namespace home {

const char* slovenianWeekday(int tmWday);
const char* slovenianMonth(int tmMon);

// Formatting functions return false and leave an empty string when the input
// is invalid or the caller-provided buffer is too small.
bool formatTime24(int hour, int minute, char* output, std::size_t outputSize);
bool formatSlovenianDate(int tmWday, int day, int tmMon, char* output,
                         std::size_t outputSize);
bool formatUnreadBadge(std::uint32_t unreadCount, char* output,
                       std::size_t outputSize);

struct AlarmStatus {
  constexpr AlarmStatus(bool enabledValue = false,
                        std::uint8_t hourValue = 0,
                        std::uint8_t minuteValue = 0)
      : enabled(enabledValue), hour(hourValue), minute(minuteValue) {}

  bool enabled;
  std::uint8_t hour;
  std::uint8_t minute;
};

// Accepts only "a off" and "a on HH MM" with two decimal digits per field.
// On failure, status is unchanged.
bool parseAlarmCommand(const char* command, AlarmStatus& status);

// Returns zero when count is zero. An out-of-range current index is treated as
// no current selection.
std::size_t chooseRandomIndex(std::size_t count, std::size_t current,
                              std::uint32_t randomValue);

}  // namespace home
