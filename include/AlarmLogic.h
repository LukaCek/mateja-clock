#pragma once

#include <cstddef>
#include <cstdint>

namespace alarmclock {

struct AlarmConfig {
  AlarmConfig();
  AlarmConfig(bool softwareEnabledValue, std::uint8_t hourValue,
              std::uint8_t minuteValue, std::uint8_t daysMaskValue,
              std::uint8_t snoozeMinutesValue, std::uint8_t volumeValue);

  bool softwareEnabled;
  std::uint8_t hour;
  std::uint8_t minute;
  std::uint8_t daysMask;
  std::uint8_t snoozeMinutes;
  std::uint8_t volume;
};

bool validateAlarmConfig(const AlarmConfig& config);
bool serializeSettingsJson(const AlarmConfig& config,
                           std::int32_t lastHandledDayKey, char* output,
                           std::size_t size);
bool parseSettingsJson(const char* input, AlarmConfig& config,
                       std::int32_t& lastHandledDayKey);

enum class AlarmState {
  Disabled,
  Armed,
  Ringing,
  Snoozed,
};

struct ClockSample {
  ClockSample();
  ClockSample(bool validValue, std::int64_t epochSecondsValue,
              int localYearValue, int localYdayValue, int tmWdayValue,
              int hourValue, int minuteValue);

  bool valid;
  std::int64_t epochSeconds;
  int localYear;
  int localYday;
  int tmWday;
  int hour;
  int minute;
};

int nextEligibleDaysOffset(int tmWday, std::uint8_t daysMask);

class AlarmEngine {
 public:
  AlarmEngine();
  explicit AlarmEngine(const AlarmConfig& config);

  const AlarmConfig& config() const;
  bool setConfig(const AlarmConfig& config);
  bool hardwareAllowed() const;
  void setHardwareAllowed(bool allowed);
  AlarmState state() const;
  std::int32_t lastHandledDayKey() const;
  void restoreLastHandledDayKey(std::int32_t dayKey);
  std::int64_t snoozeDeadline() const;

  bool update(const ClockSample& sample);
  bool snooze(const ClockSample& sample);
  bool stop(const ClockSample& sample);
  void testRing(const ClockSample& sample);

 private:
  AlarmConfig config_;
  bool hardwareAllowed_;
  AlarmState state_;
  std::int32_t lastHandledDayKey_;
  std::int64_t snoozeDeadline_;
  bool explicitTestRing_;
};

enum class AlarmSerialCommandType {
  Invalid,
  Status,
  Enable,
  Disable,
  SetTime,
  SetDays,
  Test,
  Snooze,
  Stop,
  ResetHandledDay,
};

struct AlarmSerialCommand {
  AlarmSerialCommand();

  AlarmSerialCommandType type;
  std::uint8_t hour;
  std::uint8_t minute;
  std::uint8_t daysMask;
};

AlarmSerialCommand parseAlarmSerialCommand(const char* command);

}
