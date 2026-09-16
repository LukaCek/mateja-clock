#include "AlarmService.h"

#include <SD.h>

#include <cstring>
#include <limits>

namespace {

constexpr char kConfigDirectory[] = "/clock/config";
constexpr char kSettingsPath[] = "/clock/config/settings.json";
constexpr char kBackupPath[] = "/clock/config/settings.json.bak";
constexpr char kTemporaryPath[] = "/clock/config/settings.tmp";
constexpr size_t kSettingsBufferSize = 384;

bool sameConfig(const alarmclock::AlarmConfig& left,
                const alarmclock::AlarmConfig& right) {
  return left.softwareEnabled == right.softwareEnabled &&
         left.hour == right.hour && left.minute == right.minute &&
         left.daysMask == right.daysMask &&
         left.snoozeMinutes == right.snoozeMinutes &&
         left.volume == right.volume;
}

const char* stateName(alarmclock::AlarmState state) {
  switch (state) {
    case alarmclock::AlarmState::Disabled:
      return "disabled";
    case alarmclock::AlarmState::Armed:
      return "armed";
    case alarmclock::AlarmState::Ringing:
      return "ringing";
    case alarmclock::AlarmState::Snoozed:
      return "snoozed";
  }
  return "unknown";
}

const char* evaluationReason(const TimeService::Snapshot& snapshot,
                             const alarmclock::AlarmConfig& config,
                             bool hardwareAllowed,
                             alarmclock::AlarmState state,
                             int32_t handledDayKey) {
  if (!snapshot.valid || snapshot.yearDay < 0 || snapshot.yearDay > 365 ||
      snapshot.weekday < 0 || snapshot.weekday > 6 || snapshot.hour < 0 ||
      snapshot.hour > 23 || snapshot.minute < 0 || snapshot.minute > 59) {
    return "now_invalid";
  }
  if (!config.softwareEnabled) return "blocked_by_software";
  if ((config.daysMask & (1U << ((snapshot.weekday + 6) % 7))) == 0) {
    return "wrong_weekday";
  }
  if (snapshot.hour != config.hour || snapshot.minute != config.minute) {
    return "wrong_minute";
  }
  if (!hardwareAllowed) return "blocked_by_hardware";
  const int32_t dayKey = snapshot.year * 366 + snapshot.yearDay;
  if (dayKey == handledDayKey) return "blocked_by_handled_day";
  if (state == alarmclock::AlarmState::Snoozed) return "snoozed";
  if (state == alarmclock::AlarmState::Ringing) return "ringing";
  return "eligible";
}

}

void AlarmService::begin(bool sdMounted) {
  sdMounted_ = sdMounted;
  invalidTimeLogged_ = false;
  evalStateKnown_ = false;
  lastEvalMinuteKey_ = -1;

  alarmclock::AlarmConfig loadedConfig;
  int32_t loadedDayKey = std::numeric_limits<int32_t>::min();
  bool loaded = false;
  if (sdMounted_) {
    loaded = load(kSettingsPath, loadedConfig, loadedDayKey);
    if (!loaded) {
      loaded = load(kBackupPath, loadedConfig, loadedDayKey);
      if (loaded) {
        Serial.println("[ALARM] recovered settings from backup");
        if (!save(loadedConfig, loadedDayKey)) {
          Serial.println("[ALARM] backup recovery persistence failed");
        }
      }
    }
  }

  if (!loaded) {
    loadedConfig = alarmclock::AlarmConfig();
    loadedDayKey = std::numeric_limits<int32_t>::min();
    Serial.println("[ALARM] using default settings");
  }

  const bool hardwareAllowed = engine_.hardwareAllowed();
  engine_ = alarmclock::AlarmEngine(loadedConfig);
  engine_.setHardwareAllowed(hardwareAllowed);
  engine_.restoreLastHandledDayKey(loadedDayKey);
}

const alarmclock::AlarmConfig& AlarmService::config() const {
  return engine_.config();
}

alarmclock::AlarmState AlarmService::state() const { return engine_.state(); }

home::AlarmStatus AlarmService::status() const {
  const alarmclock::AlarmConfig& current = engine_.config();
  return home::AlarmStatus(current.softwareEnabled, engine_.hardwareAllowed(),
                           current.hour, current.minute);
}

alarmclock::ClockSample AlarmService::clockSample(
    const TimeService::Snapshot& snapshot) {
  return alarmclock::ClockSample(
      snapshot.valid, static_cast<int64_t>(snapshot.epochSeconds),
      snapshot.year, snapshot.yearDay, snapshot.weekday, snapshot.hour,
      snapshot.minute);
}

bool AlarmService::update(const TimeService::Snapshot& snapshot) {
  const bool timeValid = snapshot.valid && snapshot.yearDay >= 0 &&
                         snapshot.yearDay <= 365 && snapshot.weekday >= 0 &&
                         snapshot.weekday <= 6 && snapshot.hour >= 0 &&
                         snapshot.hour <= 23 && snapshot.minute >= 0 &&
                         snapshot.minute <= 59;
  if (engine_.config().softwareEnabled && !timeValid) {
    if (!invalidTimeLogged_) {
      Serial.println("[ALARM] time invalid; alarm evaluation paused");
      invalidTimeLogged_ = true;
    }
  } else if (timeValid) {
    if (invalidTimeLogged_) {
      Serial.println("[ALARM] time valid; alarm evaluation resumed");
    }
    invalidTimeLogged_ = false;
  } else {
    invalidTimeLogged_ = false;
  }

  const alarmclock::AlarmState previous = engine_.state();
  const int32_t previousDayKey = engine_.lastHandledDayKey();
  engine_.update(clockSample(snapshot));
  const bool startedRinging = previous != alarmclock::AlarmState::Ringing &&
                               engine_.state() == alarmclock::AlarmState::Ringing;
  const bool handledDayChanged = engine_.lastHandledDayKey() != previousDayKey;
  if (handledDayChanged &&
       !persistHandledDayKey()) {
    Serial.println("[ALARM] handled day persistence failed");
  }

  const alarmclock::AlarmConfig& config = engine_.config();
  const bool hardwareAllowed = engine_.hardwareAllowed();
  const int64_t minuteKey =
      static_cast<int64_t>(snapshot.year) * 1000000LL +
      static_cast<int64_t>(snapshot.yearDay) * 1440LL +
      static_cast<int64_t>(snapshot.hour) * 60 + snapshot.minute;
  int distance = config.hour * 60 + config.minute -
                 (snapshot.hour * 60 + snapshot.minute);
  if (distance < 0) distance = -distance;
  if (distance > 720) distance = 1440 - distance;
  const bool nearScheduledMinute = timeValid && distance <= 2;
  const bool stateChanged = !evalStateKnown_ || engine_.state() != lastEvalState_;
  const bool gateChanged = !evalStateKnown_ ||
                           hardwareAllowed != lastEvalHardwareAllowed_;
  const bool handledChanged = !evalStateKnown_ ||
                              engine_.lastHandledDayKey() != lastEvalHandledDayKey_;
  const bool minuteChanged = minuteKey != lastEvalMinuteKey_;
  if ((nearScheduledMinute && minuteChanged) || stateChanged || gateChanged ||
      handledChanged) {
    const int32_t dayKey = snapshot.year * 366 + snapshot.yearDay;
    const bool snoozeRering = startedRinging &&
                              previous == alarmclock::AlarmState::Snoozed;
    const char* action = startedRinging ? (snoozeRering ? "RERING" : "RING")
                                        : "BLOCK";
    const char* reason = startedRinging ? (snoozeRering ? "snooze" : "scheduled")
                                        : evaluationReason(
                                              snapshot, config, hardwareAllowed,
                                              engine_.state(),
                                              engine_.lastHandledDayKey());
    Serial.printf(
        "[ALARM_EVAL] epoch=%lld local=%04d-%03d %02d:%02d wd=%d "
        "cfg=%02u:%02u days=%u sw=%u hw=%u eff=%u state=%s day=%ld "
        "handled=%ld snooze=%lld eligible=%u action=%s reason=%s%s\n",
        static_cast<long long>(snapshot.epochSeconds), snapshot.year,
        snapshot.yearDay, snapshot.hour, snapshot.minute, snapshot.weekday,
        static_cast<unsigned>(config.hour), static_cast<unsigned>(config.minute),
        static_cast<unsigned>(config.daysMask), config.softwareEnabled ? 1U : 0U,
        hardwareAllowed ? 1U : 0U,
        config.softwareEnabled && hardwareAllowed ? 1U : 0U,
        stateName(engine_.state()), static_cast<long>(dayKey),
        static_cast<long>(engine_.lastHandledDayKey()),
        static_cast<long long>(engine_.snoozeDeadline()),
        (startedRinging || std::strcmp(reason, "eligible") == 0) ? 1U : 0U,
        action, reason,
        !hardwareAllowed && timeValid && config.softwareEnabled &&
                (config.daysMask & (1U << ((snapshot.weekday + 6) % 7))) != 0 &&
                snapshot.hour == config.hour && snapshot.minute == config.minute
            ? " occurrence=handled"
            : "");
  }
  lastEvalMinuteKey_ = minuteKey;
  lastEvalState_ = engine_.state();
  lastEvalHardwareAllowed_ = hardwareAllowed;
  lastEvalHandledDayKey_ = engine_.lastHandledDayKey();
  evalStateKnown_ = true;
  return startedRinging;
}

bool AlarmService::applyConfig(const alarmclock::AlarmConfig& newConfig) {
  if (!alarmclock::validateAlarmConfig(newConfig)) {
    return false;
  }
  if (!save(newConfig, engine_.lastHandledDayKey())) {
    return false;
  }
  return engine_.setConfig(newConfig);
}

bool AlarmService::setEnabled(bool enabled) {
  alarmclock::AlarmConfig next = engine_.config();
  next.softwareEnabled = enabled;
  return applyConfig(next);
}

bool AlarmService::setTime(uint8_t hour, uint8_t minute) {
  alarmclock::AlarmConfig next = engine_.config();
  next.hour = hour;
  next.minute = minute;
  return applyConfig(next);
}

bool AlarmService::setDays(uint8_t daysMask) {
  alarmclock::AlarmConfig next = engine_.config();
  next.daysMask = daysMask;
  return applyConfig(next);
}

bool AlarmService::setSnoozeMinutes(uint8_t snoozeMinutes) {
  alarmclock::AlarmConfig next = engine_.config();
  next.snoozeMinutes = snoozeMinutes;
  return applyConfig(next);
}

bool AlarmService::setVolume(uint8_t volume) {
  alarmclock::AlarmConfig next = engine_.config();
  next.volume = volume;
  return applyConfig(next);
}

void AlarmService::testRing(const TimeService::Snapshot& snapshot) {
  engine_.testRing(clockSample(snapshot));
}

bool AlarmService::snooze(const TimeService::Snapshot& snapshot) {
  return engine_.snooze(clockSample(snapshot));
}

bool AlarmService::stop(const TimeService::Snapshot& snapshot) {
  if (!engine_.stop(clockSample(snapshot))) {
    return false;
  }
  if (!persistHandledDayKey()) {
    Serial.println("[ALARM] handled day persistence failed");
  }
  return true;
}

void AlarmService::setHardwareAllowed(bool allowed) {
  engine_.setHardwareAllowed(allowed);
}

bool AlarmService::hardwareAllowed() const {
  return engine_.hardwareAllowed();
}

bool AlarmService::resetHandledDay() {
  engine_.restoreLastHandledDayKey(std::numeric_limits<std::int32_t>::min());
  return persistHandledDayKey();
}

bool AlarmService::load(const char* path, alarmclock::AlarmConfig& loadedConfig,
                        int32_t& loadedDayKey) const {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t size = file.size();
  if (size == 0 || size >= kSettingsBufferSize) {
    file.close();
    Serial.printf("[ALARM] settings size invalid path=%s bytes=%u\n", path,
                  static_cast<unsigned>(size));
    return false;
  }
  char buffer[kSettingsBufferSize];
  const size_t read = file.readBytes(buffer, size);
  file.close();
  if (read != size) {
    Serial.printf("[ALARM] settings read failed path=%s bytes=%u/%u\n", path,
                  static_cast<unsigned>(read), static_cast<unsigned>(size));
    return false;
  }
  buffer[size] = '\0';
  if (!alarmclock::parseSettingsJson(buffer, loadedConfig, loadedDayKey)) {
    Serial.printf("[ALARM] settings parse failed path=%s\n", path);
    return false;
  }
  return true;
}

bool AlarmService::ensureConfigDirectory() const {
  if (SD.exists(kConfigDirectory)) {
    File directory = SD.open(kConfigDirectory, FILE_READ);
    const bool valid = directory && directory.isDirectory();
    directory.close();
    return valid;
  }
  if (!SD.exists("/clock") && !SD.mkdir("/clock")) {
    return false;
  }
  return SD.mkdir(kConfigDirectory) || SD.exists(kConfigDirectory);
}

bool AlarmService::save(const alarmclock::AlarmConfig& newConfig,
                        int32_t lastHandledDayKey) const {
  if (!sdMounted_ || !ensureConfigDirectory()) {
    return false;
  }

  char buffer[kSettingsBufferSize];
  if (!alarmclock::serializeSettingsJson(newConfig, lastHandledDayKey, buffer,
                                    sizeof(buffer))) {
    return false;
  }
  const size_t length = std::strlen(buffer);

  if (SD.exists(kTemporaryPath) && !SD.remove(kTemporaryPath)) {
    return false;
  }
  File temporary = SD.open(kTemporaryPath, FILE_WRITE);
  if (!temporary) {
    return false;
  }
  const size_t written = temporary.write(
      reinterpret_cast<const uint8_t*>(buffer), length);
  temporary.flush();
  temporary.close();
  if (written != length) {
    SD.remove(kTemporaryPath);
    return false;
  }

  alarmclock::AlarmConfig verifiedConfig;
  int32_t verifiedDayKey = std::numeric_limits<int32_t>::min();
  if (!load(kTemporaryPath, verifiedConfig, verifiedDayKey) ||
      !sameConfig(newConfig, verifiedConfig) ||
      verifiedDayKey != lastHandledDayKey) {
    SD.remove(kTemporaryPath);
    return false;
  }

  const bool hadFinal = SD.exists(kSettingsPath);
  bool backupReady = false;
  if (hadFinal) {
    if (SD.exists(kBackupPath) && !SD.remove(kBackupPath)) {
      SD.remove(kTemporaryPath);
      return false;
    }
    if (!SD.rename(kSettingsPath, kBackupPath)) {
      SD.remove(kTemporaryPath);
      return false;
    }
    backupReady = true;
  }
  if (!SD.rename(kTemporaryPath, kSettingsPath)) {
    if (backupReady && !SD.rename(kBackupPath, kSettingsPath)) {
      Serial.println("[ALARM] settings backup restore failed");
    }
    SD.remove(kTemporaryPath);
    return false;
  }
  if (backupReady && !SD.remove(kBackupPath)) {
    Serial.println("[ALARM] stale settings backup removal failed");
  }
  return true;
}

bool AlarmService::persistHandledDayKey() const {
  return save(engine_.config(), engine_.lastHandledDayKey());
}

void AlarmService::printStatus() const {
  const alarmclock::AlarmConfig& current = engine_.config();
  Serial.printf(
      "[ALARM_STATUS] enabled=%s hardware=%s state=%s time=%02u:%02u "
      "days=%u snooze=%u volume=%u handled_day=%ld sd=%s\n",
      current.softwareEnabled ? "yes" : "no",
      engine_.hardwareAllowed() ? "allowed" : "blocked",
      stateName(engine_.state()), static_cast<unsigned>(current.hour),
      static_cast<unsigned>(current.minute),
      static_cast<unsigned>(current.daysMask),
      static_cast<unsigned>(current.snoozeMinutes),
      static_cast<unsigned>(current.volume),
      static_cast<long>(engine_.lastHandledDayKey()),
      sdMounted_ ? "mounted" : "unavailable");
}
