#include <unity.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "AlarmLogic.h"

namespace {

alarmclock::ClockSample sample(std::int64_t epoch, int year, int yday, int wday,
                          int hour, int minute, bool valid = true) {
  return alarmclock::ClockSample(valid, epoch, year, yday, wday, hour, minute);
}

alarmclock::AlarmConfig enabledConfig() {
  return alarmclock::AlarmConfig(true, 7, 30, 0x1F, 10, 65);
}

void testConfigDefaultsAndValidation() {
  const alarmclock::AlarmConfig defaults;
  TEST_ASSERT_FALSE(defaults.softwareEnabled);
  TEST_ASSERT_EQUAL_UINT8(7, defaults.hour);
  TEST_ASSERT_EQUAL_UINT8(0, defaults.minute);
  TEST_ASSERT_EQUAL_HEX8(0x1F, defaults.daysMask);
  TEST_ASSERT_EQUAL_UINT8(10, defaults.snoozeMinutes);
  TEST_ASSERT_EQUAL_UINT8(65, defaults.volume);
  TEST_ASSERT_TRUE(alarmclock::validateAlarmConfig(defaults));

  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 24, 0, 1, 1, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 60, 1, 1, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 0, 0, 1, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 0, 0x80, 1, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 0, 1, 0, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 0, 1, 61, 0)));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 0, 0, 1, 1, 101)));
}

void assertConfigEqual(const alarmclock::AlarmConfig& expected,
                       const alarmclock::AlarmConfig& actual) {
  TEST_ASSERT_EQUAL(expected.softwareEnabled, actual.softwareEnabled);
  TEST_ASSERT_EQUAL_UINT8(expected.hour, actual.hour);
  TEST_ASSERT_EQUAL_UINT8(expected.minute, actual.minute);
  TEST_ASSERT_EQUAL_UINT8(expected.daysMask, actual.daysMask);
  TEST_ASSERT_EQUAL_UINT8(expected.snoozeMinutes, actual.snoozeMinutes);
  TEST_ASSERT_EQUAL_UINT8(expected.volume, actual.volume);
}

void testSettingsJsonDefaultsRoundtrip() {
  const alarmclock::AlarmConfig expected;
  char json[192];
  TEST_ASSERT_TRUE(alarmclock::serializeSettingsJson(
      expected, std::numeric_limits<std::int32_t>::min(), json, sizeof(json)));
  alarmclock::AlarmConfig actual(true, 1, 2, 3, 4, 5);
  std::int32_t dayKey = 42;
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(json, actual, dayKey));
  assertConfigEqual(expected, actual);
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(), dayKey);
}

void testSettingsJsonNondefaultRoundtrip() {
  const alarmclock::AlarmConfig expected(true, 23, 59, 0x55, 60, 100);
  char json[192];
  TEST_ASSERT_TRUE(
      alarmclock::serializeSettingsJson(expected, 741736, json, sizeof(json)));
  alarmclock::AlarmConfig actual;
  std::int32_t dayKey = 0;
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(json, actual, dayKey));
  assertConfigEqual(expected, actual);
  TEST_ASSERT_EQUAL_INT32(741736, dayKey);
  char small[8] = "value";
  TEST_ASSERT_FALSE(
      alarmclock::serializeSettingsJson(expected, dayKey, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
}

void testSettingsJsonRejectsMalformedAndPartialWithoutMutation() {
  const char* invalid[] = {
      "", "[]", "{", "{\"alarm\":{}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,"
      "\"daysMask\":31,\"snoozeMinutes\":10}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,"
      "\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65,}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,"
      "\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}} trailing",
  };
  for (const char* json : invalid) {
    alarmclock::AlarmConfig config(true, 1, 2, 3, 4, 5);
    std::int32_t dayKey = 99;
    TEST_ASSERT_FALSE(alarmclock::parseSettingsJson(json, config, dayKey));
    assertConfigEqual(alarmclock::AlarmConfig(true, 1, 2, 3, 4, 5), config);
    TEST_ASSERT_EQUAL_INT32(99, dayKey);
  }
}

void testSettingsJsonRejectsInvalidValuesAndTypes() {
  const char* invalid[] = {
      "{\"alarm\":{\"enabled\":1,\"hour\":7,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":24,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":60,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"daysMask\":0,\"snoozeMinutes\":10,\"volume\":65}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":0,\"volume\":65}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":101}}",
      "{\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65},\"lastHandledDayKey\":2147483648}",
      "{\"version\":2,\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}}",
  };
  for (const char* json : invalid) {
    alarmclock::AlarmConfig config;
    std::int32_t dayKey = 5;
    TEST_ASSERT_FALSE(alarmclock::parseSettingsJson(json, config, dayKey));
    assertConfigEqual(alarmclock::AlarmConfig(), config);
    TEST_ASSERT_EQUAL_INT32(5, dayKey);
  }
}

void testSettingsJsonAllowsUnrelatedTopLevelFieldsAndOrder() {
  const char* json =
      " { \"extra\" : [true, null, {\"x\":\"y\"}], "
      "\"lastHandledDayKey\" : -123, \"alarm\" : {"
      "\"volume\":80,\"daysMask\":64,\"enabled\":true,"
      "\"snoozeMinutes\":5,\"minute\":45,\"hour\":6},"
      "\"version\":1 } ";
  alarmclock::AlarmConfig config;
  std::int32_t dayKey = 0;
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(json, config, dayKey));
  assertConfigEqual(alarmclock::AlarmConfig(true, 6, 45, 64, 5, 80), config);
  TEST_ASSERT_EQUAL_INT32(-123, dayKey);

  const char* withoutDayKey =
      "{\"alarm\":{\"enabled\":false,\"hour\":7,\"minute\":0,"
      "\"daysMask\":31,\"snoozeMinutes\":10,\"volume\":65}}";
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(withoutDayKey, config, dayKey));
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(), dayKey);
}

void testWeekdayOffsetsUseMondayFirstMask() {
  TEST_ASSERT_EQUAL_INT(0, alarmclock::nextEligibleDaysOffset(1, 0x01));
  TEST_ASSERT_EQUAL_INT(6, alarmclock::nextEligibleDaysOffset(2, 0x01));
  TEST_ASSERT_EQUAL_INT(1, alarmclock::nextEligibleDaysOffset(0, 0x01));
  TEST_ASSERT_EQUAL_INT(0, alarmclock::nextEligibleDaysOffset(0, 0x40));
  TEST_ASSERT_EQUAL_INT(-1, alarmclock::nextEligibleDaysOffset(-1, 1));
  TEST_ASSERT_EQUAL_INT(-1, alarmclock::nextEligibleDaysOffset(7, 1));
  TEST_ASSERT_EQUAL_INT(-1, alarmclock::nextEligibleDaysOffset(1, 0));
  TEST_ASSERT_EQUAL_INT(-1, alarmclock::nextEligibleDaysOffset(1, 0x80));
}

void testScheduledAlarmTriggersOnceWithoutSecondDependency() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample monday = sample(1000, 2026, 10, 1, 7, 30);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_TRUE(engine.update(monday));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_TRUE(engine.stop(monday));
  TEST_ASSERT_FALSE(engine.update(sample(1030, 2026, 10, 1, 7, 30)));
  TEST_ASSERT_FALSE(engine.update(sample(1100, 2026, 11, 2, 7, 29)));
  TEST_ASSERT_TRUE(engine.update(sample(1160, 2026, 11, 2, 7, 30)));
}

void testScheduledAlarmTriggersOnceAcrossWholeMatchingMinute() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const int32_t dayKey = 2026 * 366 + 10;
  TEST_ASSERT_TRUE(engine.update(sample(7 * 3600 + 30 * 60, 2026, 10, 1,
                                        7, 30)));
  TEST_ASSERT_FALSE(engine.update(sample(7 * 3600 + 30 * 60 + 1, 2026, 10, 1,
                                         7, 30)));
  TEST_ASSERT_FALSE(engine.update(sample(7 * 3600 + 30 * 60 + 20, 2026, 10, 1,
                                         7, 30)));
  TEST_ASSERT_FALSE(engine.update(sample(7 * 3600 + 30 * 60 + 59, 2026, 10, 1,
                                         7, 30)));
  TEST_ASSERT_EQUAL_INT32(dayKey, engine.lastHandledDayKey());
}

void testScheduleRequiresValidSelectedLocalTime() {
  alarmclock::AlarmEngine engine(enabledConfig());
  TEST_ASSERT_FALSE(engine.update(sample(1, 2026, 10, 0, 7, 30)));
  TEST_ASSERT_FALSE(engine.update(sample(1, 2026, 10, 1, 7, 30, false)));
  TEST_ASSERT_FALSE(engine.update(sample(1, 2026, 10, 1, 7, 29)));
  TEST_ASSERT_FALSE(engine.update(sample(1, 2026, 10, 8, 7, 30)));
  TEST_ASSERT_TRUE(engine.update(sample(1, 2026, 10, 1, 7, 30)));
}

void testSnoozeUsesAbsoluteDeadlineAndRerings() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample now = sample(1000, 2026, 10, 1, 7, 30);
  TEST_ASSERT_TRUE(engine.update(now));
  TEST_ASSERT_TRUE(engine.snooze(now));
  TEST_ASSERT_EQUAL_INT64(1600, engine.snoozeDeadline());
  TEST_ASSERT_FALSE(engine.update(sample(1599, 2026, 10, 1, 7, 39)));
  TEST_ASSERT_TRUE(engine.update(sample(1600, 2026, 10, 1, 7, 40)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_EQUAL_INT64(0, engine.snoozeDeadline());
}

void testRealStopMarksValidDayAndTestStopDoesNot() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample now = sample(1000, 2026, 20, 1, 7, 30);
  TEST_ASSERT_TRUE(engine.update(now));
  TEST_ASSERT_TRUE(engine.stop(now));
  TEST_ASSERT_TRUE(engine.config().softwareEnabled);
  TEST_ASSERT_EQUAL_INT(2026 * 366 + 20, engine.lastHandledDayKey());
  TEST_ASSERT_FALSE(engine.update(now));
  TEST_ASSERT_FALSE(engine.stop(now));

  alarmclock::AlarmEngine testEngine(enabledConfig());
  testEngine.testRing(now);
  TEST_ASSERT_TRUE(testEngine.stop(now));
  TEST_ASSERT_TRUE(testEngine.config().softwareEnabled);
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(),
                          testEngine.lastHandledDayKey());
  TEST_ASSERT_TRUE(testEngine.update(now));
}

void testExplicitTestRingDoesNotConsumeLaterScheduledOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample testTime = sample(7 * 3600, 2026, 20, 1, 7, 0);
  const alarmclock::ClockSample scheduled =
      sample(7 * 3600 + 30 * 60 + 20, 2026, 20, 1, 7, 30);

  engine.testRing(testTime);
  TEST_ASSERT_TRUE(engine.stop(testTime));
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(),
                          engine.lastHandledDayKey());
  TEST_ASSERT_TRUE(engine.update(scheduled));
}

void testSnoozedExplicitTestRingDoesNotConsumeLaterScheduledOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample testTime = sample(7 * 3600, 2026, 20, 1, 7, 0);
  const alarmclock::ClockSample testRering =
      sample(7 * 3600 + 10 * 60, 2026, 20, 1, 7, 10);
  const alarmclock::ClockSample scheduled =
      sample(7 * 3600 + 30 * 60 + 20, 2026, 20, 1, 7, 30);

  engine.testRing(testTime);
  TEST_ASSERT_TRUE(engine.snooze(testTime));
  TEST_ASSERT_TRUE(engine.update(testRering));
  TEST_ASSERT_TRUE(engine.stop(testRering));
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(),
                          engine.lastHandledDayKey());
  TEST_ASSERT_TRUE(engine.update(scheduled));
}

void testDisableAndHardwareDisallowStopActivity() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample now = sample(1000, 2026, 20, 1, 7, 30);
  TEST_ASSERT_TRUE(engine.update(now));
  engine.setHardwareAllowed(false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Disabled),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_FALSE(engine.update(now));
  engine.setHardwareAllowed(true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));

  alarmclock::AlarmConfig disabled = enabledConfig();
  disabled.softwareEnabled = false;
  TEST_ASSERT_TRUE(engine.setConfig(disabled));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Disabled),
                        static_cast<int>(engine.state()));
}

void testHardwareBlockedOccurrenceIsConsumedAndDoesNotReplay() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const int32_t mondayDayKey = 2026 * 366 + 20;
  engine.setHardwareAllowed(false);
  TEST_ASSERT_FALSE(engine.update(sample(7 * 3600 + 29 * 60 + 59, 2026, 20, 1,
                                         7, 29)));
  TEST_ASSERT_EQUAL_INT32(std::numeric_limits<std::int32_t>::min(),
                          engine.lastHandledDayKey());
  TEST_ASSERT_FALSE(engine.update(
      sample(7 * 3600 + 30 * 60 + 20, 2026, 20, 1, 7, 30)));
  TEST_ASSERT_EQUAL_INT32(mondayDayKey, engine.lastHandledDayKey());

  engine.setHardwareAllowed(true);
  TEST_ASSERT_FALSE(engine.update(
      sample(7 * 3600 + 30 * 60 + 40, 2026, 20, 1, 7, 30)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_TRUE(engine.update(
      sample(24 * 3600 + 7 * 3600 + 30 * 60, 2026, 21, 2, 7, 30)));
}

void testAllDaysMaskIncludesEveryWeekday() {
  for (int wday = 0; wday < 7; ++wday) {
    alarmclock::AlarmConfig config = enabledConfig();
    config.daysMask = 0x7F;
    alarmclock::AlarmEngine engine(config);
    TEST_ASSERT_TRUE(engine.update(sample(1000 + wday, 2026, 30 + wday, wday,
                                          7, 30)));
  }
}

void testInvalidConfigurationDoesNotMutateEngine() {
  alarmclock::AlarmEngine engine(enabledConfig());
  TEST_ASSERT_FALSE(engine.setConfig(alarmclock::AlarmConfig(true, 24, 0, 1, 1, 1)));
  TEST_ASSERT_EQUAL_UINT8(7, engine.config().hour);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));
}

void testTestRingContinuesWhileSoftwareDisabledUntilStop() {
  alarmclock::AlarmEngine engine;
  const alarmclock::ClockSample invalid;
  TEST_ASSERT_FALSE(engine.snooze(invalid));
  engine.testRing(invalid);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_FALSE(engine.snooze(invalid));
  TEST_ASSERT_FALSE(engine.update(invalid));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_TRUE(engine.stop(invalid));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Disabled),
                        static_cast<int>(engine.state()));
}

void testTestRingAlwaysObeysHardwareGate() {
  alarmclock::AlarmEngine engine;
  const alarmclock::ClockSample invalid;
  engine.setHardwareAllowed(false);
  engine.testRing(invalid);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Disabled),
                        static_cast<int>(engine.state()));
  engine.setHardwareAllowed(true);
  engine.testRing(invalid);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  engine.setHardwareAllowed(false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Disabled),
                        static_cast<int>(engine.state()));
}

void testRestoreLastHandledDayKeyDeduplicatesSchedule() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample monday = sample(1000, 2026, 10, 1, 7, 30);
  engine.restoreLastHandledDayKey(2026 * 366 + 10);
  TEST_ASSERT_EQUAL_INT32(2026 * 366 + 10, engine.lastHandledDayKey());
  TEST_ASSERT_FALSE(engine.update(monday));
  TEST_ASSERT_TRUE(engine.update(sample(2000, 2026, 11, 2, 7, 30)));
}

void testResetHandledDayReenablesScheduledRing() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const alarmclock::ClockSample monday = sample(1000, 2026, 10, 1, 7, 30);
  engine.restoreLastHandledDayKey(2026 * 366 + 10);
  TEST_ASSERT_FALSE(engine.update(monday));
  engine.restoreLastHandledDayKey(std::numeric_limits<std::int32_t>::min());
  TEST_ASSERT_TRUE(engine.update(monday));
}


void assertCommand(const char* text, alarmclock::AlarmSerialCommandType type,
                    int hour = 0, int minute = 0, int mask = 0,
                    int snoozeMinutes = 0) {
  const alarmclock::AlarmSerialCommand command = alarmclock::parseAlarmSerialCommand(text);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(type), static_cast<int>(command.type));
  TEST_ASSERT_EQUAL_UINT8(hour, command.hour);
  TEST_ASSERT_EQUAL_UINT8(minute, command.minute);
  TEST_ASSERT_EQUAL_UINT8(mask, command.daysMask);
  TEST_ASSERT_EQUAL_UINT8(snoozeMinutes, command.snoozeMinutes);
}

void testSerialParserAcceptsExactCommands() {
  assertCommand("alarm", alarmclock::AlarmSerialCommandType::Status);
  assertCommand("alarm on", alarmclock::AlarmSerialCommandType::Enable);
  assertCommand("alarm off", alarmclock::AlarmSerialCommandType::Disable);
  assertCommand("alarm set 0 0", alarmclock::AlarmSerialCommandType::SetTime, 0, 0);
  assertCommand("alarm set 07 05", alarmclock::AlarmSerialCommandType::SetTime, 7, 5);
  assertCommand("alarm set 23 59", alarmclock::AlarmSerialCommandType::SetTime, 23,
                59);
  assertCommand("alarm days 1111100", alarmclock::AlarmSerialCommandType::SetDays,
                0, 0, 0x1F);
  assertCommand("alarm days 0000001", alarmclock::AlarmSerialCommandType::SetDays,
                0, 0, 0x40);
  assertCommand("alarm test", alarmclock::AlarmSerialCommandType::Test);
  assertCommand("alarm snooze", alarmclock::AlarmSerialCommandType::Snooze);
  assertCommand("alarm stop", alarmclock::AlarmSerialCommandType::Stop);
  assertCommand("alarm reset-day",
                alarmclock::AlarmSerialCommandType::ResetHandledDay);
  assertCommand("alarm snooze-min 1",
                alarmclock::AlarmSerialCommandType::SetSnoozeMinutes, 0, 0,
                0, 1);
  assertCommand("alarm snooze-min 60",
                alarmclock::AlarmSerialCommandType::SetSnoozeMinutes, 0, 0,
                0, 60);
}

void testSerialParserRejectsMalformedCommands() {
  const char* invalid[] = {
      "", " alarm", "alarm ", "alarm  on", "alarm ON", "alarm set",
      "alarm set  7 5", "alarm set 7  5", "alarm set 7 5 ",
      "alarm set 007 5", "alarm set 7 005", "alarm set 24 0",
      "alarm set 0 60", "alarm set -1 0", "alarm set a 0",
      "alarm days 0000000", "alarm days 111110", "alarm days 11111000",
       "alarm days 11111x0", "alarm days 1111100 ", "alarm unknown",
       "alarm reset", "alarm reset-day ", "alarm resetday",
       "alarm snooze-min", "alarm snooze-min ", "alarm snooze-min 0",
       "alarm snooze-min 61", "alarm snooze-min -1", "alarm snooze-min 1 ",
  };
  for (const char* text : invalid) {
    assertCommand(text, alarmclock::AlarmSerialCommandType::Invalid);
  }
  assertCommand(nullptr, alarmclock::AlarmSerialCommandType::Invalid);
}

}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testConfigDefaultsAndValidation);
  RUN_TEST(testSettingsJsonDefaultsRoundtrip);
  RUN_TEST(testSettingsJsonNondefaultRoundtrip);
  RUN_TEST(testSettingsJsonRejectsMalformedAndPartialWithoutMutation);
  RUN_TEST(testSettingsJsonRejectsInvalidValuesAndTypes);
  RUN_TEST(testSettingsJsonAllowsUnrelatedTopLevelFieldsAndOrder);
  RUN_TEST(testWeekdayOffsetsUseMondayFirstMask);
  RUN_TEST(testScheduledAlarmTriggersOnceWithoutSecondDependency);
  RUN_TEST(testScheduledAlarmTriggersOnceAcrossWholeMatchingMinute);
  RUN_TEST(testScheduleRequiresValidSelectedLocalTime);
  RUN_TEST(testSnoozeUsesAbsoluteDeadlineAndRerings);
  RUN_TEST(testRealStopMarksValidDayAndTestStopDoesNot);
  RUN_TEST(testExplicitTestRingDoesNotConsumeLaterScheduledOccurrence);
  RUN_TEST(testSnoozedExplicitTestRingDoesNotConsumeLaterScheduledOccurrence);
  RUN_TEST(testDisableAndHardwareDisallowStopActivity);
  RUN_TEST(testHardwareBlockedOccurrenceIsConsumedAndDoesNotReplay);
  RUN_TEST(testAllDaysMaskIncludesEveryWeekday);
  RUN_TEST(testInvalidConfigurationDoesNotMutateEngine);
  RUN_TEST(testTestRingContinuesWhileSoftwareDisabledUntilStop);
  RUN_TEST(testTestRingAlwaysObeysHardwareGate);
  RUN_TEST(testRestoreLastHandledDayKeyDeduplicatesSchedule);
  RUN_TEST(testResetHandledDayReenablesScheduledRing);
  RUN_TEST(testSerialParserAcceptsExactCommands);
  RUN_TEST(testSerialParserRejectsMalformedCommands);
  return UNITY_END();
}
