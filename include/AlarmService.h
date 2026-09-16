#pragma once

#include <Arduino.h>

#include "AlarmLogic.h"
#include "StatusProviders.h"
#include "TimeService.h"

class AlarmService final : public AlarmStatusProvider {
 public:
  void begin(bool sdMounted);

  const alarmclock::AlarmConfig& config() const;
  alarmclock::AlarmState state() const;
  home::AlarmStatus status() const override;

  bool update(const TimeService::Snapshot& snapshot);
  bool applyConfig(const alarmclock::AlarmConfig& config);
  bool setEnabled(bool enabled);
  bool setTime(uint8_t hour, uint8_t minute);
  bool setDays(uint8_t daysMask);
  bool setSnoozeMinutes(uint8_t snoozeMinutes);
  bool setVolume(uint8_t volume);

  void testRing(const TimeService::Snapshot& snapshot);
  bool snooze(const TimeService::Snapshot& snapshot);
  bool stop(const TimeService::Snapshot& snapshot);
  void setHardwareAllowed(bool allowed);
  bool hardwareAllowed() const;
  bool resetHandledDay();
  void printStatus() const;

 private:
  static alarmclock::ClockSample clockSample(
      const TimeService::Snapshot& snapshot);
  bool load(const char* path, alarmclock::AlarmConfig& config,
            int32_t& lastHandledDayKey) const;
  bool save(const alarmclock::AlarmConfig& config,
            int32_t lastHandledDayKey) const;
  bool ensureConfigDirectory() const;
  bool persistHandledDayKey() const;

  alarmclock::AlarmEngine engine_;
  bool sdMounted_ = false;
  bool invalidTimeLogged_ = false;
  int64_t lastEvalMinuteKey_ = -1;
  alarmclock::AlarmState lastEvalState_ = alarmclock::AlarmState::Disabled;
  bool lastEvalHardwareAllowed_ = true;
  int32_t lastEvalHandledDayKey_ = 0;
  bool evalStateKnown_ = false;
};
