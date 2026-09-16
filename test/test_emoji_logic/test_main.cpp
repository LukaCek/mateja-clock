#include <unity.h>

#include <cstring>

#include "EmojiLogic.h"

namespace {

void testEmojiToFilenameSimple() {
  // 😊 = U+1F60A
  char filename[emoji::kMaxFilenameBytes];
  TEST_ASSERT_TRUE(emoji::emojiToFilename(
      "\xF0\x9F\x98\x8A", filename, sizeof(filename)));
  TEST_ASSERT_EQUAL_STRING("1f60a", filename);
}

void testEmojiHeartWithVariationSelector() {
  // ❤ = U+2764 U+FE0F (heart + VS16)
  char filename[emoji::kMaxFilenameBytes];
  TEST_ASSERT_TRUE(emoji::emojiToFilename(
      "\xE2\x9D\xA4\xEF\xB8\x8F", filename, sizeof(filename)));
  // VS16 (FE0F) should be stripped from the filename
  TEST_ASSERT_EQUAL_STRING("2764", filename);
}

void testZwjSequence() {
  // 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467
  // (man + ZWJ + woman + ZWJ + girl)
  char filename[emoji::kMaxFilenameBytes];
  TEST_ASSERT_TRUE(emoji::emojiToFilename(
      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
      "\xE2\x80\x8D\xF0\x9F\x91\xA7",
      filename, sizeof(filename)));
  TEST_ASSERT_EQUAL_STRING("1f468-1f469-1f467", filename);
}

void testMalformedUtf8ReturnsFalse() {
  char filename[emoji::kMaxFilenameBytes] = "x";
  // Invalid continuation byte
  TEST_ASSERT_FALSE(emoji::emojiToFilename(
      "\xF0\x9F\x00\x8A", filename, sizeof(filename)));
  // Output should not be touched
  TEST_ASSERT_EQUAL_STRING("x", filename);
}

void testEmptyString() {
  char filename[emoji::kMaxFilenameBytes] = "x";
  TEST_ASSERT_FALSE(emoji::emojiToFilename("", filename, sizeof(filename)));
  TEST_ASSERT_EQUAL_STRING("x", filename);
}

void testNullInput() {
  char filename[emoji::kMaxFilenameBytes] = "x";
  TEST_ASSERT_FALSE(emoji::emojiToFilename(nullptr, filename, sizeof(filename)));
  TEST_ASSERT_EQUAL_STRING("x", filename);
}

void testSmallBufferReturnsFalse() {
  char tiny[2] = "x";
  TEST_ASSERT_FALSE(emoji::emojiToFilename(
      "\xF0\x9F\x98\x8A", tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("x", tiny);
}

void testDecodeUtf8SingleByte() {
  std::uint32_t cp = 0;
  TEST_ASSERT_EQUAL_UINT8(1, emoji::decodeUtf8("A", cp));
  TEST_ASSERT_EQUAL_UINT32(0x41, cp);
}

void testDecodeUtf8MultiByte() {
  std::uint32_t cp = 0;
  TEST_ASSERT_EQUAL_UINT8(2, emoji::decodeUtf8("\xC3\xA9", cp));  // é
  TEST_ASSERT_EQUAL_UINT32(0xE9, cp);
  
  cp = 0;
  // 😊 = U+1F60A = F0 9F 98 8A
  TEST_ASSERT_EQUAL_UINT8(4, emoji::decodeUtf8("\xF0\x9F\x98\x8A", cp));
  TEST_ASSERT_EQUAL_UINT32(0x1F60A, cp);
}

void testFormatCodepoint() {
  char buf[16];
  TEST_ASSERT_EQUAL_UINT8(4, emoji::formatCodepoint(0x2764, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2764", buf);

  buf[0] = '\0';
  TEST_ASSERT_EQUAL_UINT8(5, emoji::formatCodepoint(0x1F60A, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("1f60a", buf);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testEmojiToFilenameSimple);
  RUN_TEST(testEmojiHeartWithVariationSelector);
  RUN_TEST(testZwjSequence);
  RUN_TEST(testMalformedUtf8ReturnsFalse);
  RUN_TEST(testEmptyString);
  RUN_TEST(testNullInput);
  RUN_TEST(testSmallBufferReturnsFalse);
  RUN_TEST(testDecodeUtf8SingleByte);
  RUN_TEST(testDecodeUtf8MultiByte);
  RUN_TEST(testFormatCodepoint);
  return UNITY_END();
}