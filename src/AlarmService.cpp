#include "AlarmService.h"

#include <SD.h>

#include <cinttypes>
#include <cstring>

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
    case alarmclock::AlarmState::Armed:
      return "armed";
    case alarmclock::AlarmState::Ringing:
      return "ringing";
    case alarmclock::AlarmState::Snoozed:
      return "snoozed";
  }
  return "unknown";
}

const char* originName(alarmclock::AlarmOrigin origin) {
  switch (origin) {
    case alarmclock::AlarmOrigin::None:
      return "none";
    case alarmclock::AlarmOrigin::Scheduled:
      return "scheduled";
    case alarmclock::AlarmOrigin::Test:
      return "test";
  }
  return "unknown";
}

void formatOccurrenceKey(std::int64_t key, char* output, size_t size) {
  if (key == 0) {
    std::snprintf(output, size, "none");
    return;
  }
  std::snprintf(output, size, "%" PRId64, key);
}

const char* evaluationReason(const TimeService::Snapshot& snapshot,
                              const alarmclock::AlarmConfig& config,
                              bool hardwareAllowed,
                              alarmclock::AlarmState state,
                              std::int64_t handledOccurrenceKey) {
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
  const std::int64_t occurrenceKey = alarmclock::makeOccurrenceKey(
      snapshot.year, snapshot.month + 1, snapshot.day, config.hour,
      config.minute);
  if (occurrenceKey == handledOccurrenceKey) {
    return "occurrence_already_handled";
  }
  if (!hardwareAllowed) return "hardware_off";
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
  int64_t loadedOccurrenceKey = 0;
  bool loaded = false;
  if (sdMounted_) {
    loaded = load(kSettingsPath, loadedConfig, loadedOccurrenceKey);
    if (!loaded) {
      loaded = load(kBackupPath, loadedConfig, loadedOccurrenceKey);
      if (loaded) {
        Serial.println("[ALARM] recovered settings from backup");
        if (!save(loadedConfig, loadedOccurrenceKey)) {
          Serial.println("[ALARM] backup recovery persistence failed");
        }
      }
    }
  }

  if (!loaded) {
    loadedConfig = alarmclock::AlarmConfig();
    loadedOccurrenceKey = 0;
    Serial.println("[ALARM] using default settings");
  }

  const bool hardwareAllowed = engine_.hardwareAllowed();
  engine_ = alarmclock::AlarmEngine(loadedConfig);
  engine_.setHardwareAllowed(hardwareAllowed);
  engine_.restoreLastHandledOccurrenceKey(loadedOccurrenceKey);
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

bool AlarmService::snoozeActive() const {
  return engine_.state() == alarmclock::AlarmState::Snoozed;
}

alarmclock::ClockSample AlarmService::clockSample(
    const TimeService::Snapshot& snapshot) {
  return alarmclock::ClockSample(
      snapshot.valid, static_cast<int64_t>(snapshot.epochSeconds),
      snapshot.year, snapshot.yearDay, snapshot.weekday, snapshot.month + 1,
      snapshot.day, snapshot.hour, snapshot.minute);
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
  const std::int64_t previousHandledOccurrenceKey =
      engine_.lastHandledOccurrenceKey();
  engine_.update(clockSample(snapshot));
  const bool startedRinging = previous != alarmclock::AlarmState::Ringing &&
                               engine_.state() == alarmclock::AlarmState::Ringing;
  const bool handledOccurrenceChanged =
      engine_.lastHandledOccurrenceKey() != previousHandledOccurrenceKey;
  if (handledOccurrenceChanged && !persistHandledOccurrenceKey()) {
    Serial.println("[ALARM] handled occurrence persistence failed");
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
      engine_.lastHandledOccurrenceKey() != lastEvalHandledOccurrenceKey_;
  const bool minuteChanged = minuteKey != lastEvalMinuteKey_;
  if ((nearScheduledMinute && minuteChanged) || stateChanged || gateChanged ||
      handledChanged) {
    const std::int64_t occurrenceKey = alarmclock::makeOccurrenceKey(
        snapshot.year, snapshot.month + 1, snapshot.day, config.hour,
        config.minute);
    const bool snoozeRering = startedRinging &&
                              previous == alarmclock::AlarmState::Snoozed;
    const char* action = startedRinging ? (snoozeRering ? "RERING" : "RING")
                                        : "BLOCK";
    const char* reason = startedRinging ? (snoozeRering ? "snooze" : "scheduled")
                                        : evaluationReason(
                                               snapshot, config, hardwareAllowed,
                                               engine_.state(),
                                               engine_.lastHandledOccurrenceKey());
    Serial.printf(
        "[ALARM_EVAL] epoch=%lld local=%04d-%02d-%02d %02d:%02d wd=%d "
        "cfg=%02u:%02u occurrence=%" PRId64 " last_handled=%" PRId64
        " sw=%u hw=%u eff=%u state=%s origin=%s snooze=%lld action=%s "
        "reason=%s%s\n",
        static_cast<long long>(snapshot.epochSeconds), snapshot.year,
        snapshot.month + 1, snapshot.day, snapshot.hour, snapshot.minute,
        snapshot.weekday,
        static_cast<unsigned>(config.hour), static_cast<unsigned>(config.minute),
        occurrenceKey, engine_.lastHandledOccurrenceKey(),
        config.softwareEnabled ? 1U : 0U,
        hardwareAllowed ? 1U : 0U,
        config.softwareEnabled && hardwareAllowed ? 1U : 0U,
        stateName(engine_.state()), originName(engine_.origin()),
        static_cast<long long>(engine_.snoozeDeadline()), action, reason,
        !hardwareAllowed && timeValid && config.softwareEnabled &&
                (config.daysMask & (1U << ((snapshot.weekday + 6) % 7))) != 0 &&
                snapshot.hour == config.hour && snapshot.minute == config.minute
            ? " occurrence=consumed"
            : "");
  }
  lastEvalMinuteKey_ = minuteKey;
  lastEvalState_ = engine_.state();
  lastEvalHardwareAllowed_ = hardwareAllowed;
  lastEvalHandledOccurrenceKey_ = engine_.lastHandledOccurrenceKey();
  evalStateKnown_ = true;
  return startedRinging;
}

bool AlarmService::applyConfig(const alarmclock::AlarmConfig& newConfig) {
  if (!alarmclock::validateAlarmConfig(newConfig)) {
    return false;
  }
  if (!save(newConfig, engine_.lastHandledOccurrenceKey())) {
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
  const std::int64_t previousHandledOccurrenceKey =
      engine_.lastHandledOccurrenceKey();
  if (!engine_.stop(clockSample(snapshot))) {
    return false;
  }
  if (engine_.lastHandledOccurrenceKey() != previousHandledOccurrenceKey &&
      !persistHandledOccurrenceKey()) {
    Serial.println("[ALARM] handled occurrence persistence failed");
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
  engine_.restoreLastHandledOccurrenceKey(0);
  const bool persisted = persistHandledOccurrenceKey();
  if (persisted) {
    Serial.println("[ALARM] handled occurrence reset");
  }
  return persisted;
}

bool AlarmService::load(const char* path, alarmclock::AlarmConfig& loadedConfig,
                         int64_t& loadedOccurrenceKey) const {
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
  if (!alarmclock::parseSettingsJson(buffer, loadedConfig,
                                     loadedOccurrenceKey)) {
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
                        int64_t lastHandledOccurrenceKey) const {
  if (!sdMounted_ || !ensureConfigDirectory()) {
    return false;
  }

  char buffer[kSettingsBufferSize];
  if (!alarmclock::serializeSettingsJson(newConfig, lastHandledOccurrenceKey, buffer,
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
  int64_t verifiedOccurrenceKey = 0;
  if (!load(kTemporaryPath, verifiedConfig, verifiedOccurrenceKey) ||
      !sameConfig(newConfig, verifiedConfig) ||
      verifiedOccurrenceKey != lastHandledOccurrenceKey) {
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

bool AlarmService::persistHandledOccurrenceKey() const {
  return save(engine_.config(), engine_.lastHandledOccurrenceKey());
}

void AlarmService::printStatus() const {
  const alarmclock::AlarmConfig& current = engine_.config();
  char lastHandled[24];
  char active[24];
  formatOccurrenceKey(engine_.lastHandledOccurrenceKey(), lastHandled,
                      sizeof(lastHandled));
  formatOccurrenceKey(engine_.activeOccurrenceKey(), active, sizeof(active));
  Serial.printf(
      "[ALARM_STATUS] software_enabled=%s hardware_allowed=%s "
      "effective_enabled=%s state=%s origin=%s time=%02u:%02u days=%u "
      "snooze=%u volume=%u last_handled_occurrence=%s "
      "active_occurrence=%s sd=%s\n",
      current.softwareEnabled ? "yes" : "no",
      engine_.hardwareAllowed() ? "yes" : "no",
      current.softwareEnabled && engine_.hardwareAllowed() ? "yes" : "no",
      stateName(engine_.state()), originName(engine_.origin()),
      static_cast<unsigned>(current.hour),
      static_cast<unsigned>(current.minute),
      static_cast<unsigned>(current.daysMask),
      static_cast<unsigned>(current.snoozeMinutes),
      static_cast<unsigned>(current.volume), lastHandled, active,
      sdMounted_ ? "mounted" : "unavailable");
}
