#pragma once

#include <cstddef>
#include <cstdint>

namespace home {

const char* slovenianWeekday(int tmWday);
const char* slovenianMonth(int tmMon);

// Formatting functions return false and leave an empty string when the input
// is invalid or the caller-provided buffer is too small.
bool formatTime24(int hour, int minute, char* output, std::size_t outputSize);
bool formatRingingTime(bool valid, int hour, int minute, char* output,
                       std::size_t outputSize);
bool formatSlovenianDate(int tmWday, int day, int tmMon, char* output,
                         std::size_t outputSize);
bool formatUnreadBadge(std::uint32_t unreadCount, char* output,
                       std::size_t outputSize);

// Compact right-center Home control. It is active only while the authoritative
// alarm state is Snoozed; otherwise this region remains normal photo navigation.
bool snoozeIndicatorVisible(bool snoozed);
bool isSnoozeIndicatorTouch(std::uint16_t x, std::uint16_t y, bool snoozed);

// Describes the alarm state shown on the Home screen. `softwareEnabled` comes
// from the alarm settings; `hardwareAllowed` reflects the physical C3 alarm
// switch (when false, ringing is blocked regardless of the software setting).
struct AlarmStatus {
  // 4-arg ctor: all fields explicit.
  constexpr AlarmStatus(bool softwareEnabledValue, bool hardwareAllowedValue,
                        std::uint8_t hourValue, std::uint8_t minuteValue)
      : softwareEnabled(softwareEnabledValue),
        hardwareAllowed(hardwareAllowedValue),
        hour(hourValue),
        minute(minuteValue) {}

  // 3-arg ctor for legacy/test convenience: defaults hardwareAllowed to true.
  constexpr AlarmStatus(bool softwareEnabledValue, std::uint8_t hourValue,
                        std::uint8_t minuteValue)
      : AlarmStatus(softwareEnabledValue, true, hourValue, minuteValue) {}

  // Default: disabled + hardware allowed.
  constexpr AlarmStatus()
      : AlarmStatus(false, true, 0, 0) {}

  bool softwareEnabled;
  bool hardwareAllowed;
  std::uint8_t hour;
  std::uint8_t minute;

  // The alarm is allowed to ring right now (both gates open).
  constexpr bool ringAuthorized() const {
    return softwareEnabled && hardwareAllowed;
  }
};

// Accepts only "a off" and "a on HH MM" with two decimal digits per field.
// On failure, status is unchanged.
bool parseAlarmCommand(const char* command, AlarmStatus& status);

// Returns zero when count is zero. An out-of-range current index is treated as
// no current selection.
std::size_t chooseRandomIndex(std::size_t count, std::size_t current,
                              std::uint32_t randomValue);

}  // namespace home
