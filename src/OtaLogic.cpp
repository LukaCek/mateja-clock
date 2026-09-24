#include "OtaLogic.h"

#include <cstring>
#include <limits>

namespace otalogic {
namespace {

void skipWhitespace(const char*& input) {
  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') {
    ++input;
  }
}

bool parseString(const char*& input, char* output, std::size_t capacity) {
  skipWhitespace(input);
  if (*input != '"' || capacity == 0) {
    return false;
  }
  ++input;
  std::size_t length = 0;
  while (*input != '\0' && *input != '"') {
    const unsigned char value = static_cast<unsigned char>(*input);
    if (value < 0x20 || *input == '\\' || length + 1 >= capacity) {
      return false;
    }
    output[length++] = *input++;
  }
  if (*input != '"') {
    return false;
  }
  ++input;
  output[length] = '\0';
  return true;
}

bool parseSize(const char*& input, std::uint32_t& output) {
  skipWhitespace(input);
  if (*input < '0' || *input > '9') {
    return false;
  }
  std::uint32_t value = 0;
  const std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
  do {
    const std::uint32_t digit = static_cast<unsigned int>(*input - '0');
    if (value > (maximum - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
    ++input;
  } while (*input >= '0' && *input <= '9');
  output = value;
  return true;
}

bool parseComponent(const char*& input, std::uint32_t& output) {
  if (*input < '0' || *input > '9') {
    return false;
  }
  if (*input == '0' && input[1] >= '0' && input[1] <= '9') {
    return false;
  }
  std::uint32_t value = 0;
  do {
    value = value * 10 + static_cast<unsigned int>(*input - '0');
    if (value > kMaxVersionComponent) {
      return false;
    }
    ++input;
  } while (*input >= '0' && *input <= '9');
  output = value;
  return true;
}

bool isHex(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

}  // namespace

bool parseSemVer(const char* text, SemVer& output) {
  if (text == nullptr) {
    return false;
  }
  const char* input = text;
  SemVer parsed;
  if (!parseComponent(input, parsed.major) || *input++ != '.' ||
      !parseComponent(input, parsed.minor) || *input++ != '.' ||
      !parseComponent(input, parsed.patch) || *input != '\0') {
    return false;
  }
  output = parsed;
  return true;
}

int compareSemVer(const SemVer& left, const SemVer& right) {
  if (left.major != right.major) {
    return left.major < right.major ? -1 : 1;
  }
  if (left.minor != right.minor) {
    return left.minor < right.minor ? -1 : 1;
  }
  if (left.patch != right.patch) {
    return left.patch < right.patch ? -1 : 1;
  }
  return 0;
}

bool parseManifest(const char* json, Manifest& output) {
  if (json == nullptr) {
    return false;
  }
  const char* input = json;
  skipWhitespace(input);
  if (*input++ != '{') {
    return false;
  }

  Manifest parsed;
  unsigned int fields = 0;
  skipWhitespace(input);
  while (*input != '}') {
    char key[16];
    if (!parseString(input, key, sizeof(key))) {
      return false;
    }
    skipWhitespace(input);
    if (*input++ != ':') {
      return false;
    }

    unsigned int bit = 0;
    bool valid = false;
    if (std::strcmp(key, "product") == 0) {
      bit = 1U << 0;
      valid = parseString(input, parsed.product, sizeof(parsed.product));
    } else if (std::strcmp(key, "version") == 0) {
      bit = 1U << 1;
      valid = parseString(input, parsed.version, sizeof(parsed.version));
    } else if (std::strcmp(key, "firmware") == 0) {
      bit = 1U << 2;
      valid = parseString(input, parsed.firmware, sizeof(parsed.firmware));
    } else if (std::strcmp(key, "size") == 0) {
      bit = 1U << 3;
      valid = parseSize(input, parsed.size);
    } else if (std::strcmp(key, "sha256") == 0) {
      bit = 1U << 4;
      valid = parseString(input, parsed.sha256, sizeof(parsed.sha256));
    } else {
      return false;
    }
    if (!valid || (fields & bit) != 0) {
      return false;
    }
    fields |= bit;

    skipWhitespace(input);
    if (*input == ',') {
      ++input;
      skipWhitespace(input);
      if (*input == '}') {
        return false;
      }
    } else if (*input != '}') {
      return false;
    }
  }
  ++input;
  skipWhitespace(input);
  if (*input != '\0' || fields != 0x1FU) {
    return false;
  }
  output = parsed;
  return true;
}

ManifestError validateManifest(const Manifest& manifest,
                               std::uint32_t slotCapacity) {
  if (std::strcmp(manifest.product, "mateja-clock") != 0) {
    return ManifestError::kInvalidProduct;
  }
  SemVer version;
  if (!parseSemVer(manifest.version, version)) {
    return ManifestError::kInvalidVersion;
  }
  if (std::strcmp(manifest.firmware, "mateja-clock.bin") != 0) {
    return ManifestError::kInvalidFirmware;
  }
  if (manifest.size < kMinimumFirmwareBytes || manifest.size > slotCapacity) {
    return ManifestError::kInvalidSize;
  }
  if (std::strlen(manifest.sha256) != kSha256HexBytes) {
    return ManifestError::kInvalidSha256;
  }
  for (std::size_t index = 0; index < kSha256HexBytes; ++index) {
    if (!isHex(manifest.sha256[index])) {
      return ManifestError::kInvalidSha256;
    }
  }
  return ManifestError::kNone;
}

UpdateDecision decideUpdate(const Manifest& manifest, const SemVer& current,
                            std::uint32_t slotCapacity) {
  if (validateManifest(manifest, slotCapacity) != ManifestError::kNone) {
    return UpdateDecision::kInvalidManifest;
  }
  SemVer available;
  parseSemVer(manifest.version, available);
  const int comparison = compareSemVer(available, current);
  if (comparison < 0) {
    return UpdateDecision::kDowngrade;
  }
  if (comparison == 0) {
    return UpdateDecision::kUpToDate;
  }
  return UpdateDecision::kUpdateAvailable;
}

OtaState transition(OtaState state, OtaEvent event) {
  if (event == OtaEvent::kReset) {
    return OtaState::kIdle;
  }
  if (event == OtaEvent::kAlarmStarted) {
    if (state == OtaState::kChecking || state == OtaState::kDownloading) {
      return OtaState::kCancelled;
    }
    if (state == OtaState::kReadyToReboot) {
      return OtaState::kRebootDeferred;
    }
    return state;
  }
  if (state == OtaState::kRebootDeferred && event == OtaEvent::kAlarmStopped) {
    return OtaState::kReadyToReboot;
  }
  if (state == OtaState::kIdle && event == OtaEvent::kCheckRequested) {
    return OtaState::kChecking;
  }
  if (state == OtaState::kChecking) {
    if (event == OtaEvent::kUpdateAvailable) {
      return OtaState::kDownloading;
    }
    if (event == OtaEvent::kNoUpdate) {
      return OtaState::kIdle;
    }
  }
  if (state == OtaState::kDownloading &&
      event == OtaEvent::kDownloadSucceeded) {
    return OtaState::kReadyToReboot;
  }
  if ((state == OtaState::kChecking || state == OtaState::kDownloading) &&
      event == OtaEvent::kFailure) {
    return OtaState::kFailed;
  }
  return state;
}

bool shouldCancelTransfer(OtaState state, OtaEvent event) {
  return event == OtaEvent::kAlarmStarted &&
         (state == OtaState::kChecking || state == OtaState::kDownloading);
}

bool shouldReboot(OtaState state, bool alarmActive) {
  return state == OtaState::kReadyToReboot && !alarmActive;
}

}  // namespace otalogic
