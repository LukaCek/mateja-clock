#include "EmojiLogic.h"

#include <cstdio>
#include <cstring>

namespace emoji {
namespace {

// Returns true if this is a variation selector (VS1..VS16 or VS17..VS256).
bool isVariationSelector(std::uint32_t cp) {
  return (cp >= 0xFE00 && cp <= 0xFE0F) ||
         (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool isZwj(std::uint32_t cp) { return cp == 0x200D; }

}  // namespace

std::uint8_t decodeUtf8(const char* input, std::uint32_t& codepoint) {
  if (input == nullptr) {
    return 0;
  }
  const auto byte0 = static_cast<std::uint8_t>(input[0]);
  if (byte0 == 0) {
    return 0;
  }
  if (byte0 < 0x80) {
    codepoint = byte0;
    return 1;
  }
  if ((byte0 & 0xE0) == 0xC0 && byte0 >= 0xC2) {
    if ((input[1] & 0xC0) != 0x80) return 0;
    codepoint = ((byte0 & 0x1F) << 6) | (input[1] & 0x3F);
    return 2;
  }
  if ((byte0 & 0xF0) == 0xE0) {
    if (byte0 == 0xE0 && (input[1] & 0xE0) != 0xA0) return 0;
    if ((input[1] & 0xC0) != 0x80 || (input[2] & 0xC0) != 0x80) return 0;
    codepoint = ((byte0 & 0x0F) << 12) | ((input[1] & 0x3F) << 6) |
                (input[2] & 0x3F);
    return 3;
  }
  if ((byte0 & 0xF8) == 0xF0 && byte0 <= 0xF4) {
    if (byte0 == 0xF0 && (input[1] & 0xF0) != 0x90) return 0;
    if (byte0 == 0xF4 && (input[1] & 0xF0) != 0x80) return 0;
    if ((input[1] & 0xC0) != 0x80 || (input[2] & 0xC0) != 0x80 ||
        (input[3] & 0xC0) != 0x80) {
      return 0;
    }
    codepoint =
        ((byte0 & 0x07) << 18) | ((input[1] & 0x3F) << 12) |
        ((input[2] & 0x3F) << 6) | (input[3] & 0x3F);
    return 4;
  }
  return 0;
}

std::uint8_t decodeCodepoints(const char* input, std::size_t maxBytes,
                              std::uint32_t* codepoints,
                              std::uint8_t maxCodepoints) {
  if (input == nullptr || codepoints == nullptr || maxCodepoints == 0) {
    return 0;
  }
  std::uint8_t count = 0;
  std::size_t offset = 0;
  while (offset < maxBytes && input[offset] != '\0') {
    if (count >= maxCodepoints) {
      return 0;
    }
    std::uint32_t cp = 0;
    const std::uint8_t consumed = decodeUtf8(input + offset, cp);
    if (consumed == 0) {
      return 0;
    }
    // Skip variation selectors in the sequence; they are implicit for emoji.
    if (!isVariationSelector(cp)) {
      codepoints[count++] = cp;
    }
    offset += consumed;
  }
  return count;
}

std::uint8_t formatCodepoint(std::uint32_t codepoint, char* output,
                             std::size_t outputSize) {
  if (output == nullptr || outputSize < 2) {
    return 0;
  }
  // Count how many hex nibbles we need (minimum, no leading zeros).
  std::uint8_t nibbles = 1;
  std::uint32_t tmp = codepoint;
  while (tmp > 0xFU) {
    tmp >>= 4;
    ++nibbles;
  }
  if (outputSize < static_cast<std::size_t>(nibbles + 1)) {
    return 0;
  }
  for (std::uint8_t i = 0; i < nibbles; ++i) {
    const std::uint8_t shift = static_cast<std::uint8_t>((nibbles - 1 - i) * 4);
    const std::uint8_t nybble = static_cast<std::uint8_t>((codepoint >> shift) & 0xF);
    output[i] = nybble < 10 ? static_cast<char>('0' + nybble)
                            : static_cast<char>('a' + nybble - 10);
  }
  output[nibbles] = '\0';
  return nibbles;
}

bool makeEmojiFilename(const std::uint32_t* codepoints, std::uint8_t count,
                       char* output, std::size_t outputSize) {
  if (codepoints == nullptr || count == 0 || output == nullptr ||
      outputSize == 0) {
    return false;
  }
  // First pass: compute required length. ZWJ (200D) is not written; it only
  // causes a '-' separator between the surrounding non-ZWJ codepoints.
  std::size_t needed = 0;
  for (std::uint8_t i = 0; i < count; ++i) {
    if (isZwj(codepoints[i])) continue;
    if (needed > 0) ++needed;  // separator
    char hex[16];
    const std::uint8_t hexLen = formatCodepoint(codepoints[i], hex, sizeof(hex));
    if (hexLen == 0) return false;
    needed += hexLen;
  }
  if (needed == 0 || needed >= outputSize) {
    return false;
  }
  // Second pass: write. Skip ZWJ elements entirely.
  std::size_t written = 0;
  for (std::uint8_t i = 0; i < count; ++i) {
    if (isZwj(codepoints[i])) continue;
    if (written > 0) {
      output[written++] = '-';
    }
    char hex[16];
    const std::uint8_t hexLen = formatCodepoint(codepoints[i], hex, sizeof(hex));
    std::memcpy(output + written, hex, hexLen);
    written += hexLen;
  }
  output[written] = '\0';
  return written > 0;
}

bool emojiToFilename(const char* utf8Input, char* output,
                     std::size_t outputSize) {
  if (utf8Input == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }
  std::uint32_t codepoints[kMaxCodepoints];
  const std::uint8_t count =
      decodeCodepoints(utf8Input, std::strlen(utf8Input), codepoints,
                       kMaxCodepoints);
  if (count == 0) {
    return false;
  }
  return makeEmojiFilename(codepoints, count, output, outputSize);
}

}  // namespace emoji