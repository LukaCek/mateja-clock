#include "AlarmLogic.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace alarmclock {
namespace {

std::int32_t dayKey(const ClockSample& sample) {
  return static_cast<std::int32_t>(sample.localYear * 366 + sample.localYday);
}

bool validSample(const ClockSample& sample) {
  return sample.valid && sample.localYday >= 0 && sample.localYday <= 365 &&
         sample.tmWday >= 0 && sample.tmWday <= 6 && sample.hour >= 0 &&
         sample.hour <= 23 && sample.minute >= 0 && sample.minute <= 59;
}

std::uint8_t weekdayBit(int tmWday) {
  return static_cast<std::uint8_t>(1U << ((tmWday + 6) % 7));
}

bool parseNumber(const char* begin, const char* end, int& value) {
  const std::ptrdiff_t length = end - begin;
  if (length < 1 || length > 2) {
    return false;
  }
  value = 0;
  for (const char* current = begin; current != end; ++current) {
    if (*current < '0' || *current > '9') {
      return false;
    }
    value = value * 10 + (*current - '0');
  }
  return true;
}

void skipWhitespace(const char*& input) {
  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') {
    ++input;
  }
}

bool parseString(const char*& input, const char*& begin, std::size_t& length,
                 bool& escaped) {
  skipWhitespace(input);
  if (*input != '"') {
    return false;
  }
  ++input;
  begin = input;
  escaped = false;
  while (*input != '\0' && *input != '"') {
    const unsigned char value = static_cast<unsigned char>(*input);
    if (value < 0x20) {
      return false;
    }
    if (*input == '\\') {
      escaped = true;
      ++input;
      if (*input == '\0') {
        return false;
      }
      if (*input == 'u') {
        for (int index = 0; index < 4; ++index) {
          ++input;
          const char digit = *input;
          if (!((digit >= '0' && digit <= '9') ||
                (digit >= 'a' && digit <= 'f') ||
                (digit >= 'A' && digit <= 'F'))) {
            return false;
          }
        }
      } else if (std::strchr("\"\\/bfnrt", *input) == nullptr) {
        return false;
      }
    }
    ++input;
  }
  if (*input != '"') {
    return false;
  }
  length = static_cast<std::size_t>(input - begin);
  ++input;
  return true;
}

bool keyEquals(const char* begin, std::size_t length, bool escaped,
               const char* expected) {
  return !escaped && std::strlen(expected) == length &&
         std::strncmp(begin, expected, length) == 0;
}

bool parseInteger(const char*& input, std::int64_t& value) {
  skipWhitespace(input);
  bool negative = false;
  if (*input == '-') {
    negative = true;
    ++input;
  }
  if (*input < '0' || *input > '9') {
    return false;
  }
  if (*input == '0' && input[1] >= '0' && input[1] <= '9') {
    return false;
  }
  std::uint64_t magnitude = 0;
  const std::uint64_t limit = negative
      ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U
      : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  while (*input >= '0' && *input <= '9') {
    const unsigned digit = static_cast<unsigned>(*input - '0');
    if (magnitude > (limit - digit) / 10U) {
      return false;
    }
    magnitude = magnitude * 10U + digit;
    ++input;
  }
  if (*input == '.' || *input == 'e' || *input == 'E') {
    return false;
  }
  if (negative && magnitude == limit) {
    value = std::numeric_limits<std::int64_t>::min();
  } else {
    value = negative ? -static_cast<std::int64_t>(magnitude)
                     : static_cast<std::int64_t>(magnitude);
  }
  return true;
}

bool skipValue(const char*& input, int depth);

bool skipObject(const char*& input, int depth) {
  if (depth > 16 || *input != '{') {
    return false;
  }
  ++input;
  skipWhitespace(input);
  if (*input == '}') {
    ++input;
    return true;
  }
  for (;;) {
    const char* begin = nullptr;
    std::size_t length = 0;
    bool escaped = false;
    if (!parseString(input, begin, length, escaped)) {
      return false;
    }
    skipWhitespace(input);
    if (*input != ':') {
      return false;
    }
    ++input;
    if (!skipValue(input, depth + 1)) {
      return false;
    }
    skipWhitespace(input);
    if (*input == '}') {
      ++input;
      return true;
    }
    if (*input != ',') {
      return false;
    }
    ++input;
    skipWhitespace(input);
    if (*input == '}') {
      return false;
    }
  }
}

bool skipArray(const char*& input, int depth) {
  if (depth > 16 || *input != '[') {
    return false;
  }
  ++input;
  skipWhitespace(input);
  if (*input == ']') {
    ++input;
    return true;
  }
  for (;;) {
    if (!skipValue(input, depth + 1)) {
      return false;
    }
    skipWhitespace(input);
    if (*input == ']') {
      ++input;
      return true;
    }
    if (*input != ',') {
      return false;
    }
    ++input;
    skipWhitespace(input);
    if (*input == ']') {
      return false;
    }
  }
}

bool skipValue(const char*& input, int depth) {
  skipWhitespace(input);
  if (*input == '{') {
    return skipObject(input, depth);
  }
  if (*input == '[') {
    return skipArray(input, depth);
  }
  if (*input == '"') {
    const char* begin = nullptr;
    std::size_t length = 0;
    bool escaped = false;
    return parseString(input, begin, length, escaped);
  }
  if (std::strncmp(input, "true", 4) == 0) {
    input += 4;
    return true;
  }
  if (std::strncmp(input, "false", 5) == 0) {
    input += 5;
    return true;
  }
  if (std::strncmp(input, "null", 4) == 0) {
    input += 4;
    return true;
  }
  std::int64_t value = 0;
  return parseInteger(input, value);
}

bool parseBoolean(const char*& input, bool& value) {
  skipWhitespace(input);
  if (std::strncmp(input, "true", 4) == 0) {
    input += 4;
    value = true;
    return true;
  }
  if (std::strncmp(input, "false", 5) == 0) {
    input += 5;
    value = false;
    return true;
  }
  return false;
}

bool parseAlarmObject(const char*& input, AlarmConfig& config) {
  skipWhitespace(input);
  if (*input != '{') {
    return false;
  }
  ++input;
  unsigned fields = 0;
  for (;;) {
    skipWhitespace(input);
    if (*input == '}') {
      ++input;
      return fields == 0x3F;
    }
    const char* key = nullptr;
    std::size_t length = 0;
    bool escaped = false;
    if (!parseString(input, key, length, escaped)) {
      return false;
    }
    skipWhitespace(input);
    if (*input != ':') {
      return false;
    }
    ++input;
    std::int64_t value = 0;
    unsigned bit = 0;
    if (keyEquals(key, length, escaped, "enabled")) {
      bit = 1U;
      if (!parseBoolean(input, config.softwareEnabled)) {
        return false;
      }
    } else {
      if (keyEquals(key, length, escaped, "hour")) bit = 2U;
      else if (keyEquals(key, length, escaped, "minute")) bit = 4U;
      else if (keyEquals(key, length, escaped, "daysMask")) bit = 8U;
      else if (keyEquals(key, length, escaped, "snoozeMinutes")) bit = 16U;
      else if (keyEquals(key, length, escaped, "volume")) bit = 32U;
      if (bit == 0) {
        if (!skipValue(input, 1)) return false;
      } else {
        if (!parseInteger(input, value) || value < 0 || value > 255) return false;
        std::uint8_t converted = static_cast<std::uint8_t>(value);
        if (bit == 2U) config.hour = converted;
        else if (bit == 4U) config.minute = converted;
        else if (bit == 8U) config.daysMask = converted;
        else if (bit == 16U) config.snoozeMinutes = converted;
        else config.volume = converted;
      }
    }
    if ((fields & bit) != 0) return false;
    fields |= bit;
    skipWhitespace(input);
    if (*input == ',') {
      ++input;
      skipWhitespace(input);
      if (*input == '}') return false;
    } else if (*input != '}') {
      return false;
    }
  }
}

}

AlarmConfig::AlarmConfig()
    : softwareEnabled(false),
      hour(7),
      minute(0),
      daysMask(0x1F),
      snoozeMinutes(10),
      volume(65) {}

AlarmConfig::AlarmConfig(bool softwareEnabledValue, std::uint8_t hourValue,
                         std::uint8_t minuteValue,
                         std::uint8_t daysMaskValue,
                         std::uint8_t snoozeMinutesValue,
                         std::uint8_t volumeValue)
    : softwareEnabled(softwareEnabledValue),
      hour(hourValue),
      minute(minuteValue),
      daysMask(daysMaskValue),
      snoozeMinutes(snoozeMinutesValue),
      volume(volumeValue) {}

bool validateAlarmConfig(const AlarmConfig& config) {
  return config.hour <= 23 && config.minute <= 59 && config.daysMask != 0 &&
          config.daysMask <= 0x7F && config.snoozeMinutes >= 1 &&
          config.snoozeMinutes <= 60 && config.volume <= 100;
}

bool serializeSettingsJson(const AlarmConfig& config,
                           std::int32_t lastHandledDayKey, char* output,
                           std::size_t size) {
  if (!validateAlarmConfig(config) || output == nullptr || size == 0) {
    return false;
  }
  const int written = std::snprintf(
      output, size,
      "{\"version\":1,\"alarm\":{\"enabled\":%s,\"hour\":%u,\"minute\":%u,"
      "\"daysMask\":%u,\"snoozeMinutes\":%u,\"volume\":%u},"
      "\"lastHandledDayKey\":%ld}",
      config.softwareEnabled ? "true" : "false",
      static_cast<unsigned>(config.hour), static_cast<unsigned>(config.minute),
      static_cast<unsigned>(config.daysMask),
      static_cast<unsigned>(config.snoozeMinutes),
      static_cast<unsigned>(config.volume), static_cast<long>(lastHandledDayKey));
  if (written < 0 || static_cast<std::size_t>(written) >= size) {
    output[0] = '\0';
    return false;
  }
  return true;
}

bool parseSettingsJson(const char* input, AlarmConfig& config,
                       std::int32_t& lastHandledDayKey) {
  if (input == nullptr) {
    return false;
  }
  AlarmConfig parsedConfig;
  std::int32_t parsedDayKey = std::numeric_limits<std::int32_t>::min();
  const char* current = input;
  skipWhitespace(current);
  if (*current != '{') {
    return false;
  }
  ++current;
  bool alarmSeen = false;
  bool versionSeen = false;
  bool dayKeySeen = false;
  for (;;) {
    skipWhitespace(current);
    if (*current == '}') {
      ++current;
      break;
    }
    const char* key = nullptr;
    std::size_t length = 0;
    bool escaped = false;
    if (!parseString(current, key, length, escaped)) return false;
    skipWhitespace(current);
    if (*current != ':') return false;
    ++current;
    if (keyEquals(key, length, escaped, "alarm")) {
      if (alarmSeen || !parseAlarmObject(current, parsedConfig)) return false;
      alarmSeen = true;
    } else if (keyEquals(key, length, escaped, "version")) {
      std::int64_t value = 0;
      if (versionSeen || !parseInteger(current, value) || value != 1) return false;
      versionSeen = true;
    } else if (keyEquals(key, length, escaped, "lastHandledDayKey")) {
      std::int64_t value = 0;
      if (dayKeySeen || !parseInteger(current, value) ||
          value < std::numeric_limits<std::int32_t>::min() ||
          value > std::numeric_limits<std::int32_t>::max()) return false;
      parsedDayKey = static_cast<std::int32_t>(value);
      dayKeySeen = true;
    } else if (!skipValue(current, 1)) {
      return false;
    }
    skipWhitespace(current);
    if (*current == ',') {
      ++current;
      skipWhitespace(current);
      if (*current == '}') return false;
    } else if (*current != '}') {
      return false;
    }
  }
  skipWhitespace(current);
  if (*current != '\0' || !alarmSeen || !validateAlarmConfig(parsedConfig)) {
    return false;
  }
  config = parsedConfig;
  lastHandledDayKey = parsedDayKey;
  return true;
}

ClockSample::ClockSample()
    : valid(false),
      epochSeconds(0),
      localYear(0),
      localYday(0),
      tmWday(0),
      hour(0),
      minute(0) {}

ClockSample::ClockSample(bool validValue, std::int64_t epochSecondsValue,
                         int localYearValue, int localYdayValue,
                         int tmWdayValue, int hourValue, int minuteValue)
    : valid(validValue),
      epochSeconds(epochSecondsValue),
      localYear(localYearValue),
      localYday(localYdayValue),
      tmWday(tmWdayValue),
      hour(hourValue),
      minute(minuteValue) {}

int nextEligibleDaysOffset(int tmWday, std::uint8_t daysMask) {
  if (tmWday < 0 || tmWday > 6 || daysMask == 0 || daysMask > 0x7F) {
    return -1;
  }
  for (int offset = 0; offset < 7; ++offset) {
    if ((daysMask & weekdayBit((tmWday + offset) % 7)) != 0) {
      return offset;
    }
  }
  return -1;
}

AlarmEngine::AlarmEngine()
    : config_(),
      hardwareAllowed_(true),
      state_(AlarmState::Disabled),
      lastHandledDayKey_(std::numeric_limits<std::int32_t>::min()),
      snoozeDeadline_(0),
      explicitTestRing_(false) {}

AlarmEngine::AlarmEngine(const AlarmConfig& config)
    : config_(),
      hardwareAllowed_(true),
      state_(AlarmState::Disabled),
      lastHandledDayKey_(std::numeric_limits<std::int32_t>::min()),
      snoozeDeadline_(0),
      explicitTestRing_(false) {
  setConfig(config);
}

const AlarmConfig& AlarmEngine::config() const { return config_; }

bool AlarmEngine::setConfig(const AlarmConfig& config) {
  if (!validateAlarmConfig(config)) {
    return false;
  }
  config_ = config;
  snoozeDeadline_ = 0;
  if (!explicitTestRing_) {
    state_ = config_.softwareEnabled && hardwareAllowed_ ? AlarmState::Armed
                                                         : AlarmState::Disabled;
  }
  return true;
}

bool AlarmEngine::hardwareAllowed() const { return hardwareAllowed_; }

void AlarmEngine::setHardwareAllowed(bool allowed) {
  hardwareAllowed_ = allowed;
  if (!hardwareAllowed_) {
    state_ = AlarmState::Disabled;
    snoozeDeadline_ = 0;
    explicitTestRing_ = false;
  } else if (!config_.softwareEnabled) {
    state_ = AlarmState::Disabled;
    snoozeDeadline_ = 0;
  } else if (state_ == AlarmState::Disabled) {
    state_ = AlarmState::Armed;
  }
}

AlarmState AlarmEngine::state() const { return state_; }

std::int32_t AlarmEngine::lastHandledDayKey() const {
  return lastHandledDayKey_;
}

void AlarmEngine::restoreLastHandledDayKey(std::int32_t dayKeyValue) {
  lastHandledDayKey_ = dayKeyValue;
}

std::int64_t AlarmEngine::snoozeDeadline() const { return snoozeDeadline_; }

bool AlarmEngine::update(const ClockSample& sample) {
  if (!hardwareAllowed_) {
    state_ = AlarmState::Disabled;
    snoozeDeadline_ = 0;
    explicitTestRing_ = false;
    return false;
  }
  if (explicitTestRing_ && state_ == AlarmState::Ringing) {
    return false;
  }
  if (!config_.softwareEnabled) {
    state_ = AlarmState::Disabled;
    snoozeDeadline_ = 0;
    return false;
  }
  if (state_ == AlarmState::Disabled) {
    state_ = AlarmState::Armed;
  }
  if (state_ == AlarmState::Snoozed && validSample(sample) &&
      sample.epochSeconds >= snoozeDeadline_) {
    state_ = AlarmState::Ringing;
    snoozeDeadline_ = 0;
    return true;
  }
  if (state_ != AlarmState::Armed || !validSample(sample)) {
    return false;
  }
  if ((config_.daysMask & weekdayBit(sample.tmWday)) == 0 ||
      sample.hour != config_.hour || sample.minute != config_.minute ||
      dayKey(sample) == lastHandledDayKey_) {
    return false;
  }
  state_ = AlarmState::Ringing;
  lastHandledDayKey_ = dayKey(sample);
  return true;
}

bool AlarmEngine::snooze(const ClockSample& sample) {
  if (state_ != AlarmState::Ringing || !validSample(sample)) {
    return false;
  }
  snoozeDeadline_ = sample.epochSeconds +
                    static_cast<std::int64_t>(config_.snoozeMinutes) * 60;
  state_ = AlarmState::Snoozed;
  return true;
}

bool AlarmEngine::stop(const ClockSample& sample) {
  if (state_ != AlarmState::Ringing && state_ != AlarmState::Snoozed) {
    return false;
  }
  if (validSample(sample)) {
    lastHandledDayKey_ = dayKey(sample);
  }
  snoozeDeadline_ = 0;
  explicitTestRing_ = false;
  state_ = config_.softwareEnabled && hardwareAllowed_ ? AlarmState::Armed
                                                       : AlarmState::Disabled;
  return true;
}

void AlarmEngine::testRing(const ClockSample&) {
  if (!hardwareAllowed_) {
    state_ = AlarmState::Disabled;
    snoozeDeadline_ = 0;
    explicitTestRing_ = false;
    return;
  }
  state_ = AlarmState::Ringing;
  snoozeDeadline_ = 0;
  explicitTestRing_ = true;
}

AlarmSerialCommand::AlarmSerialCommand()
    : type(AlarmSerialCommandType::Invalid), hour(0), minute(0), daysMask(0) {}

AlarmSerialCommand parseAlarmSerialCommand(const char* command) {
  AlarmSerialCommand result;
  if (command == nullptr) {
    return result;
  }
  if (std::strcmp(command, "alarm") == 0) {
    result.type = AlarmSerialCommandType::Status;
  } else if (std::strcmp(command, "alarm on") == 0) {
    result.type = AlarmSerialCommandType::Enable;
  } else if (std::strcmp(command, "alarm off") == 0) {
    result.type = AlarmSerialCommandType::Disable;
  } else if (std::strcmp(command, "alarm test") == 0) {
    result.type = AlarmSerialCommandType::Test;
  } else if (std::strcmp(command, "alarm snooze") == 0) {
    result.type = AlarmSerialCommandType::Snooze;
  } else if (std::strcmp(command, "alarm stop") == 0) {
    result.type = AlarmSerialCommandType::Stop;
  } else if (std::strcmp(command, "alarm reset-day") == 0) {
    result.type = AlarmSerialCommandType::ResetHandledDay;
  } else if (std::strncmp(command, "alarm set ", 10) == 0) {
    const char* hourBegin = command + 10;
    const char* space = std::strchr(hourBegin, ' ');
    int hour = 0;
    int minute = 0;
    if (space != nullptr && std::strchr(space + 1, ' ') == nullptr &&
        parseNumber(hourBegin, space, hour) &&
        parseNumber(space + 1, command + std::strlen(command), minute) &&
        hour <= 23 && minute <= 59) {
      result.type = AlarmSerialCommandType::SetTime;
      result.hour = static_cast<std::uint8_t>(hour);
      result.minute = static_cast<std::uint8_t>(minute);
    }
  } else if (std::strncmp(command, "alarm days ", 11) == 0 &&
             std::strlen(command) == 18) {
    std::uint8_t mask = 0;
    bool valid = true;
    for (int index = 0; index < 7; ++index) {
      const char value = command[11 + index];
      if (value != '0' && value != '1') {
        valid = false;
      } else if (value == '1') {
        mask = static_cast<std::uint8_t>(mask | (1U << index));
      }
    }
    if (valid && mask != 0) {
      result.type = AlarmSerialCommandType::SetDays;
      result.daysMask = mask;
    }
  }
  return result;
}

}
