#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace emoji {

// Maximum codepoints in a single emoji sequence (e.g. ZWJ sequences).
constexpr std::size_t kMaxCodepoints = 8;
// Maximum hex filename length: "xxxx-xxxx-xxxx-xxxx\0"
constexpr std::size_t kMaxFilenameBytes = 48;

// Decode one UTF-8 codepoint from input.
// Returns the number of bytes consumed (1..4), or 0 on error.
// Sets codepoint to the decoded value on success.
std::uint8_t decodeUtf8(const char* input, std::uint32_t& codepoint);

// Decode a sequence of UTF-8 codepoints into an array.
// Returns the number of codepoints decoded, or 0 on error.
// Consumes at most maxBytes from input.
std::uint8_t decodeCodepoints(const char* input, std::size_t maxBytes,
                              std::uint32_t* codepoints,
                              std::uint8_t maxCodepoints);

// Build a lowercase hex filename from a codepoint sequence.
// Handles ZWJ (U+200D), VS16 (U+FE0F), and Variation Selector Supplement
// (U+FE00..U+FE0F, U+E0100..U+E01EF). Skips variation selectors in the
// filename. Joins with '-' on ZWJ.
// Returns false if the buffer is too small.
bool makeEmojiFilename(const std::uint32_t* codepoints, std::uint8_t count,
                       char* output, std::size_t outputSize);

// Convenience: decode a UTF-8 emoji string and produce the filename in one
// call. Returns false if the string is invalid or the buffer is too small.
bool emojiToFilename(const char* utf8Input, char* output,
                     std::size_t outputSize);

// Format a single codepoint as lowercase hex without 0x/U+ prefix.
// Returns the number of characters written.
std::uint8_t formatCodepoint(std::uint32_t codepoint, char* output,
                             std::size_t outputSize);

}  // namespace emoji