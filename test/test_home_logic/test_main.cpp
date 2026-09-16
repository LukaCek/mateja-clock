#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HomeLogic.h"

namespace {

void assertAlarmEqual(const home::AlarmStatus& expected,
                      const home::AlarmStatus& actual) {
  TEST_ASSERT_EQUAL(expected.softwareEnabled, actual.softwareEnabled);
  TEST_ASSERT_EQUAL_UINT8(expected.hour, actual.hour);
  TEST_ASSERT_EQUAL_UINT8(expected.minute, actual.minute);
}

void testAlarmStatusGatesAndDefaults() {
  // Defaults: disabled + hardware allowed.
  const home::AlarmStatus defaults;
  TEST_ASSERT_FALSE(defaults.softwareEnabled);
  TEST_ASSERT_TRUE(defaults.hardwareAllowed);
  TEST_ASSERT_FALSE(defaults.ringAuthorized());
  // The 3-arg form keeps the hardware gate open.
  const home::AlarmStatus enabledOnly{true, 7, 45};
  TEST_ASSERT_TRUE(enabledOnly.softwareEnabled);
  TEST_ASSERT_TRUE(enabledOnly.hardwareAllowed);
  TEST_ASSERT_TRUE(enabledOnly.ringAuthorized());

  // 4-arg form controls both gates.
  const home::AlarmStatus blocked{true, false, 7, 45};
  TEST_ASSERT_TRUE(blocked.softwareEnabled);
  TEST_ASSERT_FALSE(blocked.hardwareAllowed);
  TEST_ASSERT_FALSE(blocked.ringAuthorized());
}

void testSlovenianWeekdayMappings() {
  const char* expected[] = {"nedelja", "ponedeljek", "torek", "sreda",
                            "četrtek", "petek",      "sobota"};
  for (int index = 0; index < 7; ++index) {
    TEST_ASSERT_EQUAL_STRING(expected[index], home::slovenianWeekday(index));
  }
  TEST_ASSERT_NULL(home::slovenianWeekday(-1));
  TEST_ASSERT_NULL(home::slovenianWeekday(7));
}

void testSlovenianMonthMappings() {
  const char* expected[] = {"januar",    "februar", "marec",   "april",
                            "maj",       "junij",   "julij",   "avgust",
                            "september", "oktober", "november", "december"};
  for (int index = 0; index < 12; ++index) {
    TEST_ASSERT_EQUAL_STRING(expected[index], home::slovenianMonth(index));
  }
  TEST_ASSERT_NULL(home::slovenianMonth(-1));
  TEST_ASSERT_NULL(home::slovenianMonth(12));
}

void testTimeFormatting() {
  char output[6];
  TEST_ASSERT_TRUE(home::formatTime24(0, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("00:00", output);
  TEST_ASSERT_TRUE(home::formatTime24(7, 5, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("07:05", output);
  TEST_ASSERT_TRUE(home::formatTime24(23, 59, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("23:59", output);
}

void testTimeFormattingRejectsInvalidInputAndSmallBuffers() {
  char output[6] = "dirty";
  TEST_ASSERT_FALSE(home::formatTime24(-1, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(home::formatTime24(24, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(home::formatTime24(0, -1, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(home::formatTime24(0, 60, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);

  char small[5] = "xxxx";
  TEST_ASSERT_FALSE(home::formatTime24(12, 34, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_FALSE(home::formatTime24(12, 34, nullptr, 0));
}

void testSlovenianDateFormattingUsesUtf8() {
  char output[64];
  TEST_ASSERT_TRUE(
      home::formatSlovenianDate(4, 14, 8, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("četrtek, 14. september", output);
  const std::uint8_t expectedPrefix[] = {0xC4, 0x8D};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPrefix,
                                reinterpret_cast<std::uint8_t*>(output), 2);
}

void testSlovenianDateFormattingBoundaries() {
  char output[64];
  TEST_ASSERT_TRUE(
      home::formatSlovenianDate(0, 1, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("nedelja, 1. januar", output);
  TEST_ASSERT_TRUE(
      home::formatSlovenianDate(6, 31, 11, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("sobota, 31. december", output);
}

void testSlovenianDateFormattingRejectsInvalidInputAndTruncation() {
  char output[64] = "dirty";
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(-1, 1, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(0, 0, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(0, 32, 0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  std::strcpy(output, "dirty");
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(0, 1, 12, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);

  char small[8] = "dirty";
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(4, 14, 8, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_FALSE(
      home::formatSlovenianDate(4, 14, 8, nullptr, 0));
}

void testUnreadBadgeFormatting() {
  char output[3];
  TEST_ASSERT_TRUE(home::formatUnreadBadge(0, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
  for (std::uint32_t count = 1; count <= 9; ++count) {
    TEST_ASSERT_TRUE(home::formatUnreadBadge(count, output, sizeof(output)));
    const char expected[] = {static_cast<char>('0' + count), '\0'};
    TEST_ASSERT_EQUAL_STRING(expected, output);
  }
  TEST_ASSERT_TRUE(home::formatUnreadBadge(10, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("9+", output);
  TEST_ASSERT_TRUE(
      home::formatUnreadBadge(UINT32_MAX, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("9+", output);
}

void testUnreadBadgeFormattingChecksBufferSize() {
  char oneByte[] = {'x'};
  TEST_ASSERT_TRUE(home::formatUnreadBadge(0, oneByte, sizeof(oneByte)));
  TEST_ASSERT_EQUAL_CHAR('\0', oneByte[0]);

  oneByte[0] = 'x';
  TEST_ASSERT_FALSE(home::formatUnreadBadge(1, oneByte, sizeof(oneByte)));
  TEST_ASSERT_EQUAL_CHAR('\0', oneByte[0]);

  char twoBytes[] = {'x', '\0'};
  TEST_ASSERT_FALSE(home::formatUnreadBadge(10, twoBytes, sizeof(twoBytes)));
  TEST_ASSERT_EQUAL_STRING("", twoBytes);
  TEST_ASSERT_FALSE(home::formatUnreadBadge(0, nullptr, 0));
}

void testAlarmOnParsesBoundaryTimes() {
  home::AlarmStatus status{false, 12, 34};
  TEST_ASSERT_TRUE(home::parseAlarmCommand("a on 00 00", status));
  assertAlarmEqual(home::AlarmStatus{true, 0, 0}, status);
  TEST_ASSERT_TRUE(home::parseAlarmCommand("a on 23 59", status));
  assertAlarmEqual(home::AlarmStatus{true, 23, 59}, status);
}

void testAlarmOffPreservesConfiguredTime() {
  home::AlarmStatus status{true, 7, 45};
  TEST_ASSERT_TRUE(home::parseAlarmCommand("a off", status));
  assertAlarmEqual(home::AlarmStatus{false, 7, 45}, status);
}

void testAlarmParserRejectsInvalidCommandsWithoutMutation() {
  const char* invalid[] = {
      "",             "a",            "a on",         "a off ",
      "a off now",    "a  off",       "A off",        "a on 7 05",
      "a on 07 5",    "a on 007 05",  "a on 07 005", "a on 07:05",
      "a on 24 00",   "a on 23 60",   "a on -1 00",   "a on 00 -1",
      "a on aa 00",   "a on 00 aa",   "a on 12 30 ",  "a on 12 30x",
      " a on 12 30",  "a\ton 12 30",  "a on\t12 30", "a on 12\t30",
  };

  const home::AlarmStatus original{true, 6, 17};
  for (const char* command : invalid) {
    home::AlarmStatus status = original;
    TEST_ASSERT_FALSE_MESSAGE(home::parseAlarmCommand(command, status), command);
    assertAlarmEqual(original, status);
  }

  home::AlarmStatus status = original;
  TEST_ASSERT_FALSE(home::parseAlarmCommand(nullptr, status));
  assertAlarmEqual(original, status);
}

void testRandomIndexHandlesZeroAndOne() {
  TEST_ASSERT_EQUAL_UINT32(0, home::chooseRandomIndex(0, 0, 123));
  TEST_ASSERT_EQUAL_UINT32(0, home::chooseRandomIndex(1, 0, 123));
  TEST_ASSERT_EQUAL_UINT32(0, home::chooseRandomIndex(1, 99, UINT32_MAX));
}

void testRandomIndexAvoidsCurrentAndStaysInRange() {
  for (std::size_t count = 2; count <= 20; ++count) {
    for (std::size_t current = 0; current < count; ++current) {
      for (std::uint32_t randomValue = 0; randomValue < 100; ++randomValue) {
        const std::size_t chosen =
            home::chooseRandomIndex(count, current, randomValue);
        TEST_ASSERT_LESS_THAN_UINT32(count, chosen);
        TEST_ASSERT_NOT_EQUAL(current, chosen);
      }
    }
  }
}

void testRandomIndexIsDeterministicAndHandlesInvalidCurrent() {
  TEST_ASSERT_EQUAL_UINT32(3, home::chooseRandomIndex(5, 2, 2));
  TEST_ASSERT_EQUAL_UINT32(0, home::chooseRandomIndex(5, 2, 4));
  TEST_ASSERT_EQUAL_UINT32(4, home::chooseRandomIndex(5, 5, 9));
  TEST_ASSERT_EQUAL_UINT32(4, home::chooseRandomIndex(5, 99, 9));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testSlovenianWeekdayMappings);
  RUN_TEST(testSlovenianMonthMappings);
  RUN_TEST(testTimeFormatting);
  RUN_TEST(testTimeFormattingRejectsInvalidInputAndSmallBuffers);
  RUN_TEST(testSlovenianDateFormattingUsesUtf8);
  RUN_TEST(testSlovenianDateFormattingBoundaries);
  RUN_TEST(testSlovenianDateFormattingRejectsInvalidInputAndTruncation);
  RUN_TEST(testUnreadBadgeFormatting);
  RUN_TEST(testUnreadBadgeFormattingChecksBufferSize);
  RUN_TEST(testAlarmStatusGatesAndDefaults);
  RUN_TEST(testAlarmOnParsesBoundaryTimes);
  RUN_TEST(testAlarmOffPreservesConfiguredTime);
  RUN_TEST(testAlarmParserRejectsInvalidCommandsWithoutMutation);
  RUN_TEST(testRandomIndexHandlesZeroAndOne);
  RUN_TEST(testRandomIndexAvoidsCurrentAndStaysInRange);
  RUN_TEST(testRandomIndexIsDeterministicAndHandlesInvalidCurrent);
  return UNITY_END();
}
