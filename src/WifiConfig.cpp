#include "WifiConfig.h"

#include <cstring>

namespace wificonfig {
namespace {

bool isSpace(char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

// Finds "key" and decodes its JSON string value into 'out'. Returns false only
// when the key is present but malformed. 'present' (optional) reports whether
// a value was found.
bool extractString(const char* data, std::size_t length, const char* key,
                   char* out, std::size_t outSize, bool* present) {
  if (present != nullptr) {
    *present = false;
  }
  const std::size_t keyLength = std::strlen(key);
  for (std::size_t index = 0; index + keyLength + 2 <= length; ++index) {
    if (data[index] != '"') {
      continue;
    }
    if (std::strncmp(data + index + 1, key, keyLength) != 0) {
      continue;
    }
    if (data[index + 1 + keyLength] != '"') {
      continue;
    }
    std::size_t cursor = index + 2 + keyLength;
    while (cursor < length && isSpace(data[cursor])) {
      ++cursor;
    }
    if (cursor >= length || data[cursor] != ':') {
      continue;
    }
    ++cursor;
    while (cursor < length && isSpace(data[cursor])) {
      ++cursor;
    }
    if (cursor >= length || data[cursor] != '"') {
      return false;
    }
    if (present != nullptr) {
      *present = true;
    }
    ++cursor;
    std::size_t written = 0;
    while (cursor < length) {
      char value = data[cursor];
      if (value == '"') {
        break;
      }
      if (value == '\\') {
        ++cursor;
        if (cursor >= length) {
          return false;
        }
        switch (data[cursor]) {
          case '"':
            value = '"';
            break;
          case '\\':
            value = '\\';
            break;
          case '/':
            value = '/';
            break;
          case 'n':
            value = '\n';
            break;
          case 'r':
            value = '\r';
            break;
          case 't':
            value = '\t';
            break;
          case 'b':
            value = '\b';
            break;
          case 'f':
            value = '\f';
            break;
          default:
            return false;
        }
      }
      if (written + 1 >= outSize) {
        return false;
      }
      out[written++] = value;
      ++cursor;
    }
    if (cursor >= length) {
      return false;  // unterminated string
    }
    out[written] = '\0';
    return true;
  }
  return true;  // key not present
}

// Checks that the top-level object opened at 'start' has a matching closing
// brace, so a truncated file is rejected instead of read as a partial config.
bool objectBalanced(const char* data, std::size_t length, std::size_t start) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (std::size_t index = start; index < length; ++index) {
    const char value = data[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        inString = false;
      }
      continue;
    }
    if (value == '"') {
      inString = true;
    } else if (value == '{') {
      ++depth;
    } else if (value == '}') {
      --depth;
      if (depth == 0) {
        return true;
      }
    }
  }
  return false;
}

void copyInto(char* destination, std::size_t destinationSize,
              const char* source) {
  const std::size_t sourceLength = std::strlen(source);
  if (sourceLength >= destinationSize) {
    return;  // unreachable: extraction already bounds the value
  }
  std::memcpy(destination, source, sourceLength + 1);
}

}  // namespace

Credentials Parse(const char* data, std::size_t length) {
  Credentials result;
  if (data == nullptr || length == 0) {
    return result;
  }
  std::size_t start = 0;
  while (start < length && isSpace(data[start])) {
    ++start;
  }
  if (start >= length || data[start] != '{') {
    return result;
  }
  if (!objectBalanced(data, length, start)) {
    return result;
  }

  char ssid[kMaxSsidBytes] = {0};
  char password[kMaxPasswordBytes] = {0};
  bool ssidPresent = false;
  if (!extractString(data, length, "ssid", ssid, sizeof(ssid), &ssidPresent)) {
    return result;
  }
  if (!extractString(data, length, "password", password, sizeof(password),
                     nullptr)) {
    return result;
  }
  if (!ssidPresent || ssid[0] == '\0') {
    return result;
  }

  copyInto(result.ssid, sizeof(result.ssid), ssid);
  copyInto(result.password, sizeof(result.password), password);
  result.valid = true;
  return result;
}

}  // namespace wificonfig
