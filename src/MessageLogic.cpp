#include "MessageLogic.h"

#include <cstdio>
#include <cstring>

namespace messagelogic {
namespace {

const char kDefaultSender[] = "Luka";

const char kHeartBytes[3] = {static_cast<char>(0xEF), static_cast<char>(0x80),
                             static_cast<char>(0xA0)};

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
  std::int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    --q;
  }
  return q;
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
  if (*input == '-') {
    return false;
  }
  if (*input < '0' || *input > '9') {
    return false;
  }
  value = 0;
  while (*input >= '0' && *input <= '9') {
    value = value * 10 + (*input - '0');
    ++input;
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

unsigned int hex4(const char* digits) {
  unsigned int value = 0;
  for (int index = 0; index < 4; ++index) {
    const unsigned char c = static_cast<unsigned char>(digits[index]);
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= c - '0';
    } else if (c >= 'a' && c <= 'f') {
      value |= c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      value |= c - 'A' + 10;
    } else {
      return 0xFFFFFFFFU;
    }
  }
  return value;
}

int encodeUtf8(unsigned int code, char* out) {
  if (code <= 0x7F) {
    out[0] = static_cast<char>(code);
    return 1;
  }
  if (code <= 0x7FF) {
    out[0] = static_cast<char>(0xC0 | (code >> 6));
    out[1] = static_cast<char>(0x80 | (code & 0x3F));
    return 2;
  }
  if (code <= 0xFFFF) {
    out[0] = static_cast<char>(0xE0 | (code >> 12));
    out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (code & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (code >> 18));
  out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (code & 0x3F));
  return 4;
}

// Decodes the raw span (begin,length) of a JSON string (excluding quotes).
// Returns the decoded byte length and NUL-terminates out, or -2 when the
// output buffer is too small, or -1 when the content is not valid JSON.
int decodeJsonString(const char* begin, std::size_t length, char* out,
                     std::size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return -1;
  }
  std::size_t written = 0;
  std::size_t index = 0;
  while (index < length) {
    const char c = begin[index];
    if (c != '\\') {
      const unsigned char value = static_cast<unsigned char>(c);
      if (value < 0x20) {
        return -1;
      }
      if (written + 1 >= outSize) {
        return -2;
      }
      out[written++] = c;
      ++index;
      continue;
    }
    if (index + 1 >= length) {
      return -1;
    }
    const char escape = begin[index + 1];
    const char* literal = nullptr;
    switch (escape) {
      case '"':
        literal = "\"";
        break;
      case '\\':
        literal = "\\";
        break;
      case '/':
        literal = "/";
        break;
      case 'b':
        literal = "\b";
        break;
      case 'f':
        literal = "\f";
        break;
      case 'n':
        literal = "\n";
        break;
      case 'r':
        literal = "\r";
        break;
      case 't':
        literal = "\t";
        break;
      case 'u': {
        if (index + 6 > length) {
          return -1;
        }
        const unsigned int code = hex4(begin + index + 2);
        std::size_t advance = 6;
        unsigned int decoded = code;
        if (code >= 0xD800 && code <= 0xDBFF) {
          if (index + 12 <= length && begin[index + 6] == '\\' &&
              begin[index + 7] == 'u') {
            const unsigned int low = hex4(begin + index + 8);
            if (low >= 0xDC00 && low <= 0xDFFF) {
              decoded = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
              advance = 12;
            } else {
              return -1;
            }
          } else {
            return -1;
          }
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
          return -1;
        }
        char encoded[4];
        const int encodedLength = encodeUtf8(decoded, encoded);
        if (written + static_cast<std::size_t>(encodedLength) >= outSize) {
          return -2;
        }
        std::memcpy(out + written, encoded,
                    static_cast<std::size_t>(encodedLength));
        written += static_cast<std::size_t>(encodedLength);
        index += advance;
        continue;
      }
      default:
        return -1;
    }
    const std::size_t literalLength = std::strlen(literal);
    if (written + literalLength >= outSize) {
      return -2;
    }
    std::memcpy(out + written, literal, literalLength);
    written += literalLength;
    index += 2;
  }
  out[written] = '\0';
  return static_cast<int>(written);
}

bool isHeartAt(const char* text, std::size_t index, std::size_t length) {
  // U+2764 HEAVY BLACK HEART = E2 9D A4 (optionally followed by FE0F).
  if (length - index >= 3 && text[index] == '\xE2' &&
      text[index + 1] == '\x9D' && text[index + 2] == '\xA4') {
    return true;
  }
  // U+2665 BLACK HEART SUIT = E2 99 A5.
  return length - index >= 3 && text[index] == '\xE2' &&
         text[index + 1] == '\x99' && text[index + 2] == '\xA5';
}

int appendRaw(char* output, std::size_t size, int position, const char* text) {
  const std::size_t length = std::strlen(text);
  if (position < 0 || static_cast<std::size_t>(position) + length >= size) {
    return -1;
  }
  std::memcpy(output + position, text, length);
  output[position + length] = '\0';
  return position + static_cast<int>(length);
}

int appendJsonString(char* output, std::size_t size, int position,
                     const char* text) {
  if (position < 0) {
    return -1;
  }
  for (const unsigned char* current =
           reinterpret_cast<const unsigned char*>(text);
       *current != '\0'; ++current) {
    const char* escaped = nullptr;
    switch (*current) {
      case '"':
        escaped = "\\\"";
        break;
      case '\\':
        escaped = "\\\\";
        break;
      case '\n':
        escaped = "\\n";
        break;
      case '\r':
        escaped = "\\r";
        break;
      case '\t':
        escaped = "\\t";
        break;
      case '\b':
        escaped = "\\b";
        break;
      case '\f':
        escaped = "\\f";
        break;
      default:
        if (*current < 0x20) {
          return -1;
        }
        if (static_cast<std::size_t>(position) + 1 >= size) {
          return -1;
        }
        output[position++] = static_cast<char>(*current);
        output[position] = '\0';
        break;
    }
    if (escaped != nullptr) {
      const std::size_t length = std::strlen(escaped);
      if (static_cast<std::size_t>(position) + length >= size) {
        return -1;
      }
      std::memcpy(output + position, escaped, length);
      position += static_cast<int>(length);
      output[position] = '\0';
    }
  }
  return position;
}

int appendInteger(char* output, std::size_t size, int position,
                  std::int64_t value) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%lld",
                static_cast<long long>(value));
  return appendRaw(output, size, position, buffer);
}

int appendBoolean(char* output, std::size_t size, int position, bool value) {
  return appendRaw(output, size, position, value ? "true" : "false");
}

}  // namespace

const char* heartTokenUtf8() { return kHeartBytes; }

std::size_t normalizeHearts(char* text, std::size_t capacity) {
  if (text == nullptr || capacity == 0) {
    return 0;
  }
  const std::size_t length = std::strlen(text);
  std::size_t read = 0;
  std::size_t write = 0;
  while (read < length) {
    if (isHeartAt(text, read, length)) {
      std::size_t advanced = 3;
      // U+2764 U+FE0F (E2 9D A4 EF B8 8F) folds to a single heart.
      if (read + 6 <= length && text[read] == '\xE2' &&
          text[read + 1] == '\x9D' && text[read + 2] == '\xA4' &&
          text[read + 3] == '\xEF' && text[read + 4] == '\xB8' &&
          text[read + 5] == '\x8F') {
        advanced = 6;
      }
      if (write <= capacity - kHeartTokenLength) {
        std::memcpy(text + write, kHeartBytes, kHeartTokenLength);
        write += kHeartTokenLength;
      }
      read += advanced;
      continue;
    }
    if (read + 3 <= length && text[read] == '\xEF' && text[read + 1] == '\xB8' &&
        text[read + 2] == '\x8F') {
      read += 3;  // stray variation selector is dropped
      continue;
    }
    text[write++] = text[read++];
  }
  if (write < capacity) {
    text[write] = '\0';
  }
  return write;
}

std::size_t copyPreviewUtf8(const char* input, char* output,
                            std::size_t outputSize, std::size_t maxBytes) {
  if (output == nullptr || outputSize == 0) {
    return 0;
  }
  output[0] = '\0';
  if (input == nullptr || maxBytes == 0) {
    return 0;
  }
  std::size_t written = 0;
  std::size_t index = 0;
  const std::size_t length = std::strlen(input);
  while (index < length && written < maxBytes && written + 1 < outputSize) {
    const unsigned char c = static_cast<unsigned char>(input[index]);
    std::size_t sequence = 1;
    if ((c & 0x80) == 0x00) {
      sequence = 1;
    } else if ((c & 0xE0) == 0xC0) {
      sequence = 2;
    } else if ((c & 0xF0) == 0xE0) {
      sequence = 3;
    } else if ((c & 0xF8) == 0xF0) {
      sequence = 4;
    } else {
      ++index;  // stray continuation byte
      continue;
    }
    if (index + sequence > length) {
      break;
    }
    if (written + sequence > maxBytes || written + sequence >= outputSize) {
      break;
    }
    bool valid = true;
    for (std::size_t k = 1; k < sequence; ++k) {
      if ((static_cast<unsigned char>(input[index + k]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      ++index;
      continue;
    }
    std::memcpy(output + written, input + index, sequence);
    written += sequence;
    index += sequence;
  }
  output[written] = '\0';
  return written;
}

IngestResult preIngestLine(const char* line, std::size_t length,
                           InboxMessage& output) {
  if (line == nullptr) {
    return IngestResult::kMalformed;
  }
  if (length == 0) {
    return IngestResult::kIgnoredEvent;
  }
  if (length > kMaxLineBytes) {
    return IngestResult::kOversizeLine;
  }
  const char* current = line;
  skipWhitespace(current);
  if (*current != '{') {
    return IngestResult::kMalformed;
  }
  ++current;

  bool eventSeen = false;
  bool idSeen = false;
  bool timeSeen = false;
  bool titleSeen = false;
  bool messageSeen = false;
  const char* eventRaw = nullptr;
  std::size_t eventLength = 0;
  bool eventEscaped = false;
  const char* idRaw = nullptr;
  std::size_t idLength = 0;
  bool idEscaped = false;
  const char* titleRaw = nullptr;
  std::size_t titleLength = 0;
  bool titleEscaped = false;
  const char* messageRaw = nullptr;
  std::size_t messageLength = 0;
  bool messageEscaped = false;
  std::int64_t timeValue = 0;

  for (;;) {
    skipWhitespace(current);
    if (*current == '}') {
      ++current;
      break;
    }
    const char* key = nullptr;
    std::size_t keyLength = 0;
    bool keyEscaped = false;
    if (!parseString(current, key, keyLength, keyEscaped)) {
      return IngestResult::kMalformed;
    }
    skipWhitespace(current);
    if (*current != ':') {
      return IngestResult::kMalformed;
    }
    ++current;
    if (keyEquals(key, keyLength, keyEscaped, "event")) {
      if (eventSeen || !parseString(current, eventRaw, eventLength,
                                    eventEscaped)) {
        return IngestResult::kMalformed;
      }
      eventSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "id")) {
      if (idSeen || !parseString(current, idRaw, idLength, idEscaped)) {
        return IngestResult::kMalformed;
      }
      idSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "time")) {
      if (timeSeen || !parseInteger(current, timeValue)) {
        return IngestResult::kMalformed;
      }
      timeSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "title")) {
      if (titleSeen || !parseString(current, titleRaw, titleLength,
                                    titleEscaped)) {
        return IngestResult::kMalformed;
      }
      titleSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "message")) {
      if (messageSeen || !parseString(current, messageRaw, messageLength,
                                      messageEscaped)) {
        return IngestResult::kMalformed;
      }
      messageSeen = true;
    } else if (!skipValue(current, 1)) {
      return IngestResult::kMalformed;
    }
    skipWhitespace(current);
    if (*current == ',') {
      ++current;
      skipWhitespace(current);
      if (*current == '}') {
        return IngestResult::kMalformed;
      }
    } else if (*current != '}') {
      return IngestResult::kMalformed;
    }
  }
  skipWhitespace(current);
  if (*current != '\0' || !eventSeen) {
    return IngestResult::kMalformed;
  }
  if (!(eventLength == 7 && std::strncmp(eventRaw, "message", 7) == 0)) {
    return IngestResult::kIgnoredEvent;
  }
  if (!idSeen || idEscaped || idLength == 0 || idLength >= kMaxIdBytes ||
      !timeSeen || !messageSeen) {
    return IngestResult::kMalformed;
  }
  char senderBuffer[kMaxTitleBytes];
  char textBuffer[kMaxTextBytes];
  if (titleSeen) {
    const int decoded = decodeJsonString(titleRaw, titleLength, senderBuffer,
                                         sizeof(senderBuffer));
    if (decoded == -2) {
      return IngestResult::kOversize;
    }
    if (decoded < 0) {
      return IngestResult::kMalformed;
    }
  } else {
    std::memcpy(senderBuffer, kDefaultSender, sizeof(kDefaultSender));
  }
  const int textLength =
      decodeJsonString(messageRaw, messageLength, textBuffer, sizeof(textBuffer));
  if (textLength == -2) {
    return IngestResult::kOversize;
  }
  if (textLength < 0) {
    return IngestResult::kMalformed;
  }

  InboxMessage result;
  std::memcpy(result.id, idRaw, idLength);
  result.id[idLength] = '\0';
  result.time = timeValue;
  const std::size_t senderLength =
      normalizeHearts(senderBuffer, sizeof(senderBuffer));
  std::memcpy(result.sender, senderBuffer, senderLength + 1);
  const std::size_t textNormalizedLength =
      normalizeHearts(textBuffer, sizeof(textBuffer));
  std::memcpy(result.text, textBuffer, textNormalizedLength + 1);
  output = result;
  return IngestResult::kAccepted;
}

bool containsId(const InboxMessage* messages, std::size_t count,
                const char* id) {
  if (id == nullptr) {
    return false;
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (std::strcmp(messages[index].id, id) == 0) {
      return true;
    }
  }
  return false;
}

std::size_t pruneToLimit(InboxMessage* messages, std::size_t count,
                         std::size_t limit) {
  while (count > limit && count > 0) {
    std::size_t oldestRead = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < count; ++index) {
      if (!messages[index].read) {
        continue;
      }
      if (oldestRead == static_cast<std::size_t>(-1) ||
          messages[index].time < messages[oldestRead].time) {
        oldestRead = index;
      }
    }
    if (oldestRead == static_cast<std::size_t>(-1)) {
      break;  // all unread: never silently drop them
    }
    std::memmove(messages + oldestRead, messages + oldestRead + 1,
                 (count - oldestRead - 1) * sizeof(InboxMessage));
    --count;
  }
  return count;
}

std::size_t unreadCount(const InboxMessage* messages, std::size_t count) {
  std::size_t result = 0;
  for (std::size_t index = 0; index < count; ++index) {
    if (!messages[index].read) {
      ++result;
    }
  }
  return result;
}

int firstPendingAckIndex(const InboxMessage* messages, std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) {
    if (messages[index].read && !messages[index].seenAckSent) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int serializeMessageJson(const InboxMessage& message, char* output,
                         std::size_t size) {
  if (output == nullptr || size == 0) {
    return -1;
  }
  output[0] = '\0';
  int position = appendRaw(output, size, 0, "{\"id\":\"");
  position = appendJsonString(output, size, position, message.id);
  position = appendRaw(output, size, position, "\",\"time\":");
  position = appendInteger(output, size, position, message.time);
  position = appendRaw(output, size, position, ",\"sender\":\"");
  position = appendJsonString(output, size, position, message.sender);
  position = appendRaw(output, size, position, "\",\"text\":\"");
  position = appendJsonString(output, size, position, message.text);
  position = appendRaw(output, size, position, "\",\"read\":");
  position = appendBoolean(output, size, position, message.read);
  position = appendRaw(output, size, position, ",\"seen\":");
  position = appendBoolean(output, size, position, message.seenAckSent);
  position = appendRaw(output, size, position, "}");
  return position;
}

bool parseMessageJson(const char* line, InboxMessage& output) {
  if (line == nullptr) {
    return false;
  }
  const char* current = line;
  skipWhitespace(current);
  if (*current != '{') {
    return false;
  }
  ++current;
  bool idSeen = false;
  bool timeSeen = false;
  bool titleSeen = false;
  bool textSeen = false;
  bool readSeen = false;
  bool seenSeen = false;
  const char* idRaw = nullptr;
  std::size_t idLength = 0;
  bool idEscaped = false;
  const char* titleRaw = nullptr;
  std::size_t titleLength = 0;
  bool titleEscaped = false;
  const char* textRaw = nullptr;
  std::size_t textLength = 0;
  bool textEscaped = false;
  std::int64_t timeValue = 0;
  bool readValue = false;
  bool seenValue = false;
  for (;;) {
    skipWhitespace(current);
    if (*current == '}') {
      ++current;
      break;
    }
    const char* key = nullptr;
    std::size_t keyLength = 0;
    bool keyEscaped = false;
    if (!parseString(current, key, keyLength, keyEscaped)) {
      return false;
    }
    skipWhitespace(current);
    if (*current != ':') {
      return false;
    }
    ++current;
    if (keyEquals(key, keyLength, keyEscaped, "id")) {
      if (idSeen || !parseString(current, idRaw, idLength, idEscaped)) {
        return false;
      }
      idSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "time")) {
      if (timeSeen || !parseInteger(current, timeValue)) {
        return false;
      }
      timeSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "sender")) {
      if (titleSeen || !parseString(current, titleRaw, titleLength,
                                    titleEscaped)) {
        return false;
      }
      titleSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "text")) {
      if (textSeen || !parseString(current, textRaw, textLength, textEscaped)) {
        return false;
      }
      textSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "read")) {
      if (readSeen || !parseBoolean(current, readValue)) {
        return false;
      }
      readSeen = true;
    } else if (keyEquals(key, keyLength, keyEscaped, "seen")) {
      if (seenSeen || !parseBoolean(current, seenValue)) {
        return false;
      }
      seenSeen = true;
    } else if (!skipValue(current, 1)) {
      return false;
    }
    skipWhitespace(current);
    if (*current == ',') {
      ++current;
      skipWhitespace(current);
      if (*current == '}') {
        return false;
      }
    } else if (*current != '}') {
      return false;
    }
  }
  skipWhitespace(current);
  if (*current != '\0' || !idSeen || idEscaped || idLength == 0 ||
      idLength >= kMaxIdBytes || !timeSeen || !textSeen) {
    return false;
  }
  InboxMessage parsed;
  std::memcpy(parsed.id, idRaw, idLength);
  parsed.id[idLength] = '\0';
  parsed.time = timeValue;
  char senderBuffer[kMaxTitleBytes];
  char textBuffer[kMaxTextBytes];
  if (titleSeen) {
    const int decoded = decodeJsonString(titleRaw, titleLength, senderBuffer,
                                         sizeof(senderBuffer));
    if (decoded < 0) {
      return false;
    }
  } else {
    std::memcpy(senderBuffer, kDefaultSender, sizeof(kDefaultSender));
  }
  if (decodeJsonString(textRaw, textLength, textBuffer, sizeof(textBuffer)) < 0) {
    return false;
  }
  const std::size_t senderLength = normalizeHearts(senderBuffer, sizeof(senderBuffer));
  std::memcpy(parsed.sender, senderBuffer, senderLength + 1);
  const std::size_t textNormalizedLength = normalizeHearts(textBuffer, sizeof(textBuffer));
  std::memcpy(parsed.text, textBuffer, textNormalizedLength + 1);
  parsed.read = readSeen && readValue;
  parsed.seenAckSent = seenSeen && seenValue;
  output = parsed;
  return true;
}

int makeSeenAckId(const char* originalId, char* output, std::size_t size) {
  int position = appendRaw(output, size, 0, "seen_");
  if (position < 0) {
    return -1;
  }
  return appendJsonString(output, size, position, originalId);
}

int makeSeenAckBody(const char* ackTopic, const char* ackSequenceId,
                    const char* preview, char* output, std::size_t size) {
  if (output == nullptr || size == 0) {
    return -1;
  }
  output[0] = '\0';
  int position = appendRaw(output, size, 0, "{\"topic\":\"");
  position = appendJsonString(output, size, position, ackTopic);
  position = appendRaw(output, size, position,
                       "\",\"title\":\"Mateja je prebrala sporočilo\",\"message\":\"");
  position = appendJsonString(output, size, position, preview);
  position = appendRaw(output, size, position, "\",\"sequence_id\":\"");
  position = appendJsonString(output, size, position, ackSequenceId);
  position = appendRaw(output, size, position, "\",\"priority\":3}");
  return position;
}

bool makeSinceParam(const char* lastId, char* output, std::size_t size) {
  if (output == nullptr || size == 0) {
    return false;
  }
  output[0] = '\0';
  if (lastId == nullptr || lastId[0] == '\0') {
    return false;
  }
  const std::size_t lastLength = std::strlen(lastId);
  if (6 + lastLength + 1 > size) {
    return false;
  }
  std::memcpy(output, "since=", 6);
  std::memcpy(output + 6, lastId, lastLength);
  output[6 + lastLength] = '\0';
  return true;
}

bool formatMessageTimeLabel(std::int64_t messageTime, std::int64_t nowEpoch,
                            std::int32_t localOffsetSeconds, bool timeValid,
                            char* output, std::size_t size) {
  if (output == nullptr || size == 0) {
    return false;
  }
  output[0] = '\0';
  if (!timeValid) {
    return false;
  }
  const std::int64_t messageLocal = messageTime + localOffsetSeconds;
  const std::int64_t nowLocal = nowEpoch + localOffsetSeconds;
  const std::int64_t difference = nowLocal - messageLocal;
  if (difference >= 0 && difference <= 60) {
    return appendRaw(output, size, 0, "zdaj") >= 0;
  }
  const std::int64_t messageDay = floorDiv(messageLocal, 86400);
  const std::int64_t nowDay = floorDiv(nowLocal, 86400);
  if (messageDay == nowDay) {
    const int hour =
        static_cast<int>(floorDiv(messageLocal % 86400, 3600));
    const int minute =
        static_cast<int>(floorDiv(messageLocal % 3600, 60));
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return appendRaw(output, size, 0, buffer) >= 0;
  }
  if (nowDay - messageDay == 1) {
    return appendRaw(output, size, 0, "včeraj") >= 0;
  }
  int year = 0;
  int month = 1;
  int day = 1;
  civilFromDays(messageDay, year, month, day);
  static const char* const months[] = {
      "jan.", "feb.", "mar.", "apr.", "maj.", "jun.",
      "jul.", "avg.", "sep.", "okt.", "nov.", "dec.",
  };
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d. %s", day,
                months[month - 1]);
  return appendRaw(output, size, 0, buffer) >= 0;
}

PopupVerdict popupVerdict(bool homeActive, bool ringingActive,
                          bool settingsActive, bool listActive,
                          bool detailActive, bool popupShowing,
                          const char* shownId, const char* incomingId) {
  if (!homeActive || ringingActive || settingsActive || listActive ||
      detailActive) {
    return PopupVerdict::kSkip;
  }
  if (popupShowing) {
    if (shownId != nullptr && incomingId != nullptr &&
        std::strcmp(shownId, incomingId) == 0) {
      return PopupVerdict::kKeep;
    }
    return PopupVerdict::kShow;
  }
  return PopupVerdict::kShow;
}

std::int64_t daysFromCivil(int year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const std::int64_t era =
      floorDiv(static_cast<std::int64_t>(year), 400LL);
  const unsigned yoe = static_cast<unsigned>(year - era * 400);  // [0, 399]
  const unsigned doy =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const unsigned doe =
      yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
  return era * 146097 + doe - 719468;
}

void civilFromDays(std::int64_t days, int& year, int& month, int& day) {
  std::int64_t z = days + 719468;
  const std::int64_t era = floorDiv(z, 146097LL);
  const unsigned doe = static_cast<unsigned>(z - era * 146097);  // [0, 146096]
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  year = static_cast<int>(yoe + era * 400);
  const unsigned doy =
      doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153;      // [0, 11]
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);  // [1, 31]
  month = static_cast<int>(mp + (mp < 10 ? 3 : -9));     // [1, 12]
  if (month <= 2) {
    ++year;
  }
}

}  // namespace messagelogic