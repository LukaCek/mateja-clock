#include "HomeLogic.h"

#include <cstdio>

namespace home {
namespace {

constexpr const char* kWeekdays[] = {
    "nedelja", "ponedeljek", "torek", "sreda",
    "četrtek", "petek",      "sobota",
};

constexpr const char* kMonths[] = {
    "januar",    "februar", "marec",   "april",
    "maj",       "junij",   "julij",   "avgust",
    "september", "oktober", "november", "december",
};

void clearOutput(char* output, std::size_t outputSize) {
  if (output != nullptr && outputSize > 0) {
    output[0] = '\0';
  }
}

bool isDigit(char value) { return value >= '0' && value <= '9'; }

}  // namespace

const char* slovenianWeekday(int tmWday) {
  if (tmWday < 0 || tmWday >= 7) {
    return nullptr;
  }
  return kWeekdays[tmWday];
}

const char* slovenianMonth(int tmMon) {
  if (tmMon < 0 || tmMon >= 12) {
    return nullptr;
  }
  return kMonths[tmMon];
}

bool formatTime24(int hour, int minute, char* output, std::size_t outputSize) {
  clearOutput(output, outputSize);
  if (output == nullptr || outputSize < 6 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59) {
    return false;
  }

  output[0] = static_cast<char>('0' + hour / 10);
  output[1] = static_cast<char>('0' + hour % 10);
  output[2] = ':';
  output[3] = static_cast<char>('0' + minute / 10);
  output[4] = static_cast<char>('0' + minute % 10);
  output[5] = '\0';
  return true;
}

bool formatSlovenianDate(int tmWday, int day, int tmMon, char* output,
                         std::size_t outputSize) {
  clearOutput(output, outputSize);
  const char* weekday = slovenianWeekday(tmWday);
  const char* month = slovenianMonth(tmMon);
  if (output == nullptr || outputSize == 0 || weekday == nullptr ||
      month == nullptr || day < 1 || day > 31) {
    return false;
  }

  const int written =
      std::snprintf(output, outputSize, "%s, %d. %s", weekday, day, month);
  if (written < 0 || static_cast<std::size_t>(written) >= outputSize) {
    output[0] = '\0';
    return false;
  }
  return true;
}

bool formatUnreadBadge(std::uint32_t unreadCount, char* output,
                       std::size_t outputSize) {
  clearOutput(output, outputSize);
  if (output == nullptr || outputSize == 0) {
    return false;
  }
  if (unreadCount == 0) {
    return true;
  }
  if (unreadCount <= 9) {
    if (outputSize < 2) {
      return false;
    }
    output[0] = static_cast<char>('0' + unreadCount);
    output[1] = '\0';
    return true;
  }
  if (outputSize < 3) {
    return false;
  }
  output[0] = '9';
  output[1] = '+';
  output[2] = '\0';
  return true;
}

bool parseAlarmCommand(const char* command, AlarmStatus& status) {
  if (command == nullptr) {
    return false;
  }

  if (command[0] == 'a' && command[1] == ' ' && command[2] == 'o' &&
      command[3] == 'f' && command[4] == 'f' && command[5] == '\0') {
    status.enabled = false;
    return true;
  }

  if (command[0] != 'a' || command[1] != ' ' || command[2] != 'o' ||
      command[3] != 'n' || command[4] != ' ' || !isDigit(command[5]) ||
      !isDigit(command[6]) || command[7] != ' ' || !isDigit(command[8]) ||
      !isDigit(command[9]) || command[10] != '\0') {
    return false;
  }

  const int hour = (command[5] - '0') * 10 + command[6] - '0';
  const int minute = (command[8] - '0') * 10 + command[9] - '0';
  if (hour > 23 || minute > 59) {
    return false;
  }

  AlarmStatus parsed = status;
  parsed.enabled = true;
  parsed.hour = static_cast<std::uint8_t>(hour);
  parsed.minute = static_cast<std::uint8_t>(minute);
  status = parsed;
  return true;
}

std::size_t chooseRandomIndex(std::size_t count, std::size_t current,
                              std::uint32_t randomValue) {
  if (count <= 1) {
    return 0;
  }
  if (current >= count) {
    return static_cast<std::size_t>(randomValue) % count;
  }

  std::size_t chosen = static_cast<std::size_t>(randomValue) % (count - 1);
  if (chosen >= current) {
    ++chosen;
  }
  return chosen;
}

}  // namespace home
