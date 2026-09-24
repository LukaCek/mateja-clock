#include "NtfyConfig.h"

#include <cstring>

namespace ntfyconfig {
namespace {

class Parser {
 public:
  Parser(const char* data, std::size_t length)
      : data_(data), length_(length), position_(0) {}

  bool parse(Config& output) {
    if (data_ == nullptr || length_ == 0 || length_ > kMaxConfigBytes) {
      return false;
    }
    skipSpace();
    if (!consume('{')) {
      return false;
    }
    skipSpace();
    if (consume('}')) {
      return false;
    }

    unsigned fields = 0;
    for (;;) {
      char key[32] = {0};
      if (!parseString(key, sizeof(key))) {
        return false;
      }
      skipSpace();
      if (!consume(':')) {
        return false;
      }
      skipSpace();

      unsigned field = 0;
      char* destination = nullptr;
      std::size_t capacity = 0;
      if (std::strcmp(key, "base_url") == 0) {
        field = 1U << 0;
        destination = output.baseUrl;
        capacity = sizeof(output.baseUrl);
      } else if (std::strcmp(key, "inbox_topic") == 0) {
        field = 1U << 1;
        destination = output.inboxTopic;
        capacity = sizeof(output.inboxTopic);
      } else if (std::strcmp(key, "ack_topic") == 0) {
        field = 1U << 2;
        destination = output.ackTopic;
        capacity = sizeof(output.ackTopic);
      } else if (std::strcmp(key, "access_token") == 0) {
        field = 1U << 3;
        destination = output.accessToken;
        capacity = sizeof(output.accessToken);
      } else if (std::strcmp(key, "ca_cert") == 0) {
        field = 1U << 4;
        destination = output.caCert;
        capacity = sizeof(output.caCert);
      }

      if (field != 0) {
        if ((fields & field) != 0 || !parseString(destination, capacity)) {
          return false;
        }
        fields |= field;
      } else if (!skipValue(0)) {
        return false;
      }

      skipSpace();
      if (consume('}')) {
        break;
      }
      if (!consume(',')) {
        return false;
      }
      skipSpace();
    }

    skipSpace();
    const unsigned required = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3);
    if (position_ != length_ || (fields & required) != required ||
        !validBaseUrl(output.baseUrl) || !validTopic(output.inboxTopic) ||
        (output.ackTopic[0] != '\0' && !validTopic(output.ackTopic)) ||
        !validHeaderValue(output.accessToken)) {
      return false;
    }
    if (output.ackTopic[0] == '\0') {
      std::memcpy(output.ackTopic, output.inboxTopic,
                  std::strlen(output.inboxTopic) + 1);
    }
    output.valid = true;
    return true;
  }

 private:
  bool parseString(char* output, std::size_t capacity) {
    if (capacity == 0 || !consume('"')) {
      return false;
    }
    std::size_t written = 0;
    while (position_ < length_) {
      const unsigned char value =
          static_cast<unsigned char>(data_[position_++]);
      if (value == '"') {
        output[written] = '\0';
        return true;
      }
      if (value < 0x20) {
        return false;
      }
      if (value != '\\') {
        if (!appendByte(value, output, capacity, written)) {
          return false;
        }
        continue;
      }
      if (position_ >= length_) {
        return false;
      }
      const char escaped = data_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          if (!appendByte(static_cast<unsigned char>(escaped), output, capacity,
                          written)) {
            return false;
          }
          break;
        case 'b':
          if (!appendByte('\b', output, capacity, written)) return false;
          break;
        case 'f':
          if (!appendByte('\f', output, capacity, written)) return false;
          break;
        case 'n':
          if (!appendByte('\n', output, capacity, written)) return false;
          break;
        case 'r':
          if (!appendByte('\r', output, capacity, written)) return false;
          break;
        case 't':
          if (!appendByte('\t', output, capacity, written)) return false;
          break;
        case 'u': {
          unsigned codePoint = 0;
          if (!parseHex4(codePoint)) {
            return false;
          }
          if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            if (position_ + 2 > length_ || data_[position_] != '\\' ||
                data_[position_ + 1] != 'u') {
              return false;
            }
            position_ += 2;
            unsigned low = 0;
            if (!parseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
              return false;
            }
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) +
                        (low - 0xDC00);
          } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
            return false;
          }
          if (!appendUtf8(codePoint, output, capacity, written)) {
            return false;
          }
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool skipValue(unsigned depth) {
    if (depth > 16 || position_ >= length_) {
      return false;
    }
    if (data_[position_] == '"') {
      return skipString();
    }
    if (data_[position_] == '{') {
      ++position_;
      skipSpace();
      if (consume('}')) return true;
      for (;;) {
        if (!skipString()) return false;
        skipSpace();
        if (!consume(':')) return false;
        skipSpace();
        if (!skipValue(depth + 1)) return false;
        skipSpace();
        if (consume('}')) return true;
        if (!consume(',')) return false;
        skipSpace();
      }
    }
    if (data_[position_] == '[') {
      ++position_;
      skipSpace();
      if (consume(']')) return true;
      for (;;) {
        if (!skipValue(depth + 1)) return false;
        skipSpace();
        if (consume(']')) return true;
        if (!consume(',')) return false;
        skipSpace();
      }
    }
    if (matchLiteral("true") || matchLiteral("false") ||
        matchLiteral("null")) {
      return true;
    }
    return skipNumber();
  }

  bool skipString() {
    if (!consume('"')) return false;
    while (position_ < length_) {
      const unsigned char value =
          static_cast<unsigned char>(data_[position_++]);
      if (value == '"') return true;
      if (value < 0x20) return false;
      if (value == '\\') {
        if (position_ >= length_) return false;
        const char escaped = data_[position_++];
        if (escaped == 'u') {
          unsigned ignored = 0;
          if (!parseHex4(ignored)) return false;
        } else if (std::strchr("\"\\/bfnrt", escaped) == nullptr) {
          return false;
        }
      }
    }
    return false;
  }

  bool skipNumber() {
    const std::size_t start = position_;
    if (consume('-') && position_ >= length_) return false;
    if (consume('0')) {
      if (position_ < length_ && data_[position_] >= '0' &&
          data_[position_] <= '9') return false;
    } else {
      if (position_ >= length_ || data_[position_] < '1' ||
          data_[position_] > '9') return false;
      while (position_ < length_ && data_[position_] >= '0' &&
             data_[position_] <= '9') ++position_;
    }
    if (consume('.')) {
      const std::size_t digits = position_;
      while (position_ < length_ && data_[position_] >= '0' &&
             data_[position_] <= '9') ++position_;
      if (digits == position_) return false;
    }
    if (position_ < length_ &&
        (data_[position_] == 'e' || data_[position_] == 'E')) {
      ++position_;
      if (position_ < length_ &&
          (data_[position_] == '+' || data_[position_] == '-')) ++position_;
      const std::size_t digits = position_;
      while (position_ < length_ && data_[position_] >= '0' &&
             data_[position_] <= '9') ++position_;
      if (digits == position_) return false;
    }
    return position_ > start;
  }

  bool parseHex4(unsigned& value) {
    if (position_ + 4 > length_) return false;
    value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      const char digit = data_[position_++];
      value <<= 4;
      if (digit >= '0' && digit <= '9') value += digit - '0';
      else if (digit >= 'a' && digit <= 'f') value += digit - 'a' + 10;
      else if (digit >= 'A' && digit <= 'F') value += digit - 'A' + 10;
      else return false;
    }
    return true;
  }

  static bool appendByte(unsigned char value, char* output,
                         std::size_t capacity, std::size_t& written) {
    if (written + 1 >= capacity) return false;
    output[written++] = static_cast<char>(value);
    return true;
  }

  static bool appendUtf8(unsigned value, char* output, std::size_t capacity,
                         std::size_t& written) {
    if (value == 0) {
      return false;
    }
    if (value <= 0x7F) {
      return appendByte(static_cast<unsigned char>(value), output, capacity,
                        written);
    }
    unsigned char bytes[4];
    std::size_t count = 0;
    if (value <= 0x7FF) {
      bytes[0] = static_cast<unsigned char>(0xC0 | (value >> 6));
      bytes[1] = static_cast<unsigned char>(0x80 | (value & 0x3F));
      count = 2;
    } else if (value <= 0xFFFF) {
      bytes[0] = static_cast<unsigned char>(0xE0 | (value >> 12));
      bytes[1] = static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3F));
      bytes[2] = static_cast<unsigned char>(0x80 | (value & 0x3F));
      count = 3;
    } else if (value <= 0x10FFFF) {
      bytes[0] = static_cast<unsigned char>(0xF0 | (value >> 18));
      bytes[1] = static_cast<unsigned char>(0x80 | ((value >> 12) & 0x3F));
      bytes[2] = static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3F));
      bytes[3] = static_cast<unsigned char>(0x80 | (value & 0x3F));
      count = 4;
    } else {
      return false;
    }
    if (written + count >= capacity) return false;
    for (std::size_t index = 0; index < count; ++index) {
      output[written++] = static_cast<char>(bytes[index]);
    }
    return true;
  }

  bool matchLiteral(const char* literal) {
    const std::size_t size = std::strlen(literal);
    if (position_ + size > length_ ||
        std::strncmp(data_ + position_, literal, size) != 0) return false;
    position_ += size;
    return true;
  }

  bool consume(char expected) {
    if (position_ >= length_ || data_[position_] != expected) return false;
    ++position_;
    return true;
  }

  void skipSpace() {
    while (position_ < length_ &&
           (data_[position_] == ' ' || data_[position_] == '\t' ||
            data_[position_] == '\n' || data_[position_] == '\r')) {
      ++position_;
    }
  }

  static bool validBaseUrl(const char* value) {
    static const char prefix[] = "https://";
    if (std::strncmp(value, prefix, sizeof(prefix) - 1) != 0) return false;
    const char* authority = value + sizeof(prefix) - 1;
    if (*authority == '\0') return false;
    for (const char* cursor = authority; *cursor != '\0'; ++cursor) {
      const unsigned char character = static_cast<unsigned char>(*cursor);
      if (character <= 0x20 || character == 0x7F || character == '/' ||
          character == '?' || character == '#' || character == '@') {
        return false;
      }
    }
    const char* port = nullptr;
    if (*authority == '[') {
      return false;  // NtfyClient's host splitter does not support IPv6.
    } else {
      const char* colon = std::strrchr(authority, ':');
      if (colon != nullptr) {
        if (colon == authority) return false;
        port = colon + 1;
      }
    }
    if (port != nullptr) {
      if (*port == '\0') return false;
      unsigned number = 0;
      for (const char* cursor = port; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        number = number * 10 + static_cast<unsigned>(*cursor - '0');
        if (number > 65535) return false;
      }
      if (number == 0) return false;
    }
    return true;
  }

  static bool validTopic(const char* value) {
    if (value[0] == '\0') return false;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(value);
         *cursor != '\0'; ++cursor) {
      if (*cursor <= 0x20 || *cursor == 0x7F || *cursor == '/' ||
          *cursor == '?' || *cursor == '#') {
        return false;
      }
    }
    return true;
  }

  static bool validHeaderValue(const char* value) {
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(value);
         *cursor != '\0'; ++cursor) {
      if (*cursor < 0x20 || *cursor == 0x7F) return false;
    }
    return true;
  }

  const char* data_;
  std::size_t length_;
  std::size_t position_;
};

}  // namespace

Config Parse(const char* data, std::size_t length) {
  Config result;
  Parser parser(data, length);
  if (!parser.parse(result)) {
    return Config();
  }
  return result;
}

}  // namespace ntfyconfig
