#include <unity.h>

#include <cstdint>
#include <cstring>

#include "AlarmLogic.h"

namespace {

alarmclock::ClockSample sample(std::int64_t epoch, int year, int month, int day,
                               int yday, int wday, int hour, int minute,
                               bool valid = true) {
  return alarmclock::ClockSample(valid, epoch, year, yday, wday, month, day,
                                 hour, minute);
}

alarmclock::AlarmConfig enabledConfig(int hour = 7, int minute = 30) {
  return alarmclock::AlarmConfig(true, static_cast<std::uint8_t>(hour),
                                 static_cast<std::uint8_t>(minute), 0x7F, 10,
                                 65);
}

std::int64_t key(int year, int month, int day, int hour, int minute) {
  return alarmclock::makeOccurrenceKey(year, month, day, hour, minute);
}

void testConfigDefaultsAndValidation() {
  const alarmclock::AlarmConfig defaults;
  TEST_ASSERT_FALSE(defaults.softwareEnabled);
  TEST_ASSERT_EQUAL_UINT8(10, defaults.snoozeMinutes);
  TEST_ASSERT_TRUE(alarmclock::validateAlarmConfig(defaults));
  TEST_ASSERT_FALSE(alarmclock::validateAlarmConfig(
      alarmclock::AlarmConfig(true, 24, 0, 1, 1, 0)));
}

void testOccurrenceKeyUsesLocalCivilDate() {
  TEST_ASSERT_EQUAL_INT64(202609161700, key(2026, 9, 16, 17, 0));
  // A UTC time near midnight can be 00:30 on the next Europe/Ljubljana date.
  TEST_ASSERT_EQUAL_INT64(202609170030, key(2026, 9, 17, 0, 30));
  TEST_ASSERT_EQUAL_INT64(0, key(2026, 0, 17, 0, 30));
}

void testSettingsOccurrenceRoundtrip64Bit() {
  const alarmclock::AlarmConfig expected = enabledConfig(17, 0);
  char json[256];
  TEST_ASSERT_TRUE(alarmclock::serializeSettingsJson(
      expected, 202609161700LL, json, sizeof(json)));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "handled_occurrence"));
  alarmclock::AlarmConfig actual;
  std::int64_t occurrence = 0;
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(json, actual, occurrence));
  TEST_ASSERT_EQUAL_UINT8(17, actual.hour);
  TEST_ASSERT_EQUAL_INT64(202609161700LL, occurrence);
}

void testLegacyDayKeyDoesNotBlockSchedule() {
  const char* legacy =
      "{\"version\":1,\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":30,"
      "\"daysMask\":127,\"snoozeMinutes\":10,\"volume\":65},"
      "\"lastHandledDayKey\":741774}";
  alarmclock::AlarmConfig config;
  std::int64_t occurrence = 99;
  TEST_ASSERT_TRUE(alarmclock::parseSettingsJson(legacy, config, occurrence));
  TEST_ASSERT_EQUAL_INT64(0, occurrence);
  alarmclock::AlarmEngine engine(config);
  TEST_ASSERT_TRUE(engine.update(sample(27020, 2026, 9, 16, 258, 3, 7, 30)));
}

void testNormalOccurrenceRingsAtTwentySeconds() {
  alarmclock::AlarmEngine engine(enabledConfig());
  TEST_ASSERT_TRUE(engine.update(sample(27020, 2026, 9, 16, 258, 3, 7, 30)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmOrigin::Scheduled),
                        static_cast<int>(engine.origin()));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.activeOccurrenceKey());
}

void testStopHandlesOnlyExactOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto at20 = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(at20));
  TEST_ASSERT_TRUE(engine.stop(at20));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.lastHandledOccurrenceKey());
  TEST_ASSERT_FALSE(engine.update(sample(27040, 2026, 9, 16, 258, 3, 7, 30)));
}

void testSameDayAlarmTimeChangeRings() {
  alarmclock::AlarmEngine engine(enabledConfig(7, 30));
  const auto morning = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(morning));
  TEST_ASSERT_TRUE(engine.stop(morning));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.lastHandledOccurrenceKey());
  TEST_ASSERT_TRUE(engine.setConfig(enabledConfig(17, 0)));
  TEST_ASSERT_TRUE(engine.update(sample(61220, 2026, 9, 16, 258, 3, 17, 0)));
  TEST_ASSERT_EQUAL_INT64(202609161700LL, engine.activeOccurrenceKey());
}

void testTestStopDoesNotPoisonRealSchedule() {
  alarmclock::AlarmEngine engine(enabledConfig(17, 0));
  const auto earlier = sample(36000, 2026, 9, 16, 258, 3, 10, 0);
  engine.testRing(earlier);
  TEST_ASSERT_TRUE(engine.stop(earlier));
  TEST_ASSERT_EQUAL_INT64(0, engine.lastHandledOccurrenceKey());
  TEST_ASSERT_TRUE(engine.update(sample(61220, 2026, 9, 16, 258, 3, 17, 0)));
}

void testTestSnoozeLifecycleDoesNotPoisonRealSchedule() {
  alarmclock::AlarmEngine engine(enabledConfig(17, 0));
  const auto testTime = sample(36000, 2026, 9, 16, 258, 3, 10, 0);
  engine.testRing(testTime);
  TEST_ASSERT_TRUE(engine.snooze(testTime));
  TEST_ASSERT_TRUE(engine.update(sample(36600, 2026, 9, 16, 258, 3, 10, 10)));
  TEST_ASSERT_TRUE(engine.stop(sample(36601, 2026, 9, 16, 258, 3, 10, 10)));
  TEST_ASSERT_EQUAL_INT64(0, engine.lastHandledOccurrenceKey());
  TEST_ASSERT_TRUE(engine.update(sample(61220, 2026, 9, 16, 258, 3, 17, 0)));
}

void testScheduledSnoozePreservesActiveOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.snooze(scheduled));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.activeOccurrenceKey());
  TEST_ASSERT_TRUE(engine.update(sample(27620, 2026, 9, 16, 258, 3, 7, 40)));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.activeOccurrenceKey());
}

void testHardwareBlockedOccurrenceAndChangedTime() {
  alarmclock::AlarmEngine engine(enabledConfig());
  engine.setHardwareAllowed(false);
  TEST_ASSERT_FALSE(engine.update(sample(27020, 2026, 9, 16, 258, 3, 7, 30)));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.lastHandledOccurrenceKey());
  engine.setHardwareAllowed(true);
  TEST_ASSERT_FALSE(engine.update(sample(27040, 2026, 9, 16, 258, 3, 7, 30)));
  TEST_ASSERT_TRUE(engine.setConfig(enabledConfig(17, 0)));
  TEST_ASSERT_TRUE(engine.update(sample(61220, 2026, 9, 16, 258, 3, 17, 0)));
}

void testNextDaySameTimeRings() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto first = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(first));
  TEST_ASSERT_TRUE(engine.stop(first));
  TEST_ASSERT_TRUE(engine.update(sample(113420, 2026, 9, 17, 259, 4, 7, 30)));
}

void testMatchingMinuteIsSecondIndependent() {
  const std::int64_t seconds[] = {27000, 27001, 27020, 27059};
  for (const std::int64_t second : seconds) {
    alarmclock::AlarmEngine engine(enabledConfig());
    TEST_ASSERT_TRUE(engine.update(sample(second, 2026, 9, 16, 258, 3, 7, 30)));
    TEST_ASSERT_TRUE(engine.stop(sample(second, 2026, 9, 16, 258, 3, 7, 30)));
    TEST_ASSERT_FALSE(engine.update(sample(27059, 2026, 9, 16, 258, 3, 7, 30)));
  }
}

void testWrongMinuteAndWeekdayDoNotRing() {
  alarmclock::AlarmEngine engine(enabledConfig());
  TEST_ASSERT_FALSE(engine.update(sample(26940, 2026, 9, 16, 258, 3, 7, 29)));
  TEST_ASSERT_FALSE(engine.update(sample(27060, 2026, 9, 16, 258, 3, 7, 31)));
  auto config = enabledConfig();
  config.daysMask = 0x01;
  TEST_ASSERT_TRUE(engine.setConfig(config));
  TEST_ASSERT_FALSE(engine.update(sample(27020, 2026, 9, 16, 258, 3, 7, 30)));
}

void testSoftwareDisabledDoesNotConsumeOccurrence() {
  auto config = enabledConfig();
  config.softwareEnabled = false;
  alarmclock::AlarmEngine engine(config);
  TEST_ASSERT_FALSE(engine.update(sample(27020, 2026, 9, 16, 258, 3, 7, 30)));
  TEST_ASSERT_EQUAL_INT64(0, engine.lastHandledOccurrenceKey());
}

void testHardwareOffWhileRingingResolvesScheduledOnly() {
  alarmclock::AlarmEngine scheduled(enabledConfig());
  const auto now = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(scheduled.update(now));
  TEST_ASSERT_TRUE(scheduled.stop(now));
  scheduled.setHardwareAllowed(false);
  scheduled.setHardwareAllowed(true);
  TEST_ASSERT_EQUAL_INT64(202609160730LL,
                          scheduled.lastHandledOccurrenceKey());

  alarmclock::AlarmEngine test(enabledConfig());
  test.testRing(now);
  TEST_ASSERT_TRUE(test.stop(now));
  test.setHardwareAllowed(false);
  TEST_ASSERT_EQUAL_INT64(0, test.lastHandledOccurrenceKey());
}

void testPersistenceRestoreBlocksOnlySameOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig(17, 0));
  engine.restoreLastHandledOccurrenceKey(202609161700LL);
  TEST_ASSERT_FALSE(engine.update(sample(61220, 2026, 9, 16, 258, 3, 17, 0)));
  TEST_ASSERT_TRUE(engine.setConfig(enabledConfig(18, 0)));
  TEST_ASSERT_TRUE(engine.update(sample(64820, 2026, 9, 16, 258, 3, 18, 0)));
}

void testResetOccurrenceReenablesOnlyThatOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  engine.restoreLastHandledOccurrenceKey(202609160730LL);
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_FALSE(engine.update(scheduled));
  engine.restoreLastHandledOccurrenceKey(0);
  TEST_ASSERT_TRUE(engine.update(scheduled));
}

void testTestRingUsesTestOriginAndNoActiveOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  engine.testRing(sample(1, 2026, 9, 16, 258, 3, 6, 0));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmOrigin::Test),
                        static_cast<int>(engine.origin()));
  TEST_ASSERT_EQUAL_INT64(0, engine.activeOccurrenceKey());
}

void testSnoozeDeadlineUsesConfiguredMinutes() {
  auto config = enabledConfig();
  config.snoozeMinutes = 5;
  alarmclock::AlarmEngine engine(config);
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.snooze(scheduled));
  TEST_ASSERT_EQUAL_INT64(27320, engine.snoozeDeadline());
}

void testHardwareOffUnrelatedMinuteDoesNotConsume() {
  alarmclock::AlarmEngine engine(enabledConfig());
  engine.setHardwareAllowed(false);
  TEST_ASSERT_FALSE(engine.update(sample(26999, 2026, 9, 16, 258, 3, 7, 29)));
  TEST_ASSERT_EQUAL_INT64(0, engine.lastHandledOccurrenceKey());
}

void testChangingVolumeDoesNotClearHandledOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  engine.restoreLastHandledOccurrenceKey(202609160730LL);
  auto config = engine.config();
  config.volume = 10;
  TEST_ASSERT_TRUE(engine.setConfig(config));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.lastHandledOccurrenceKey());
}

void testInvalidSampleDoesNotTriggerSnoozedAlarm() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.snooze(scheduled));
  TEST_ASSERT_FALSE(engine.update(sample(27620, 2026, 9, 16, 258, 3, 7, 40,
                                         false)));
}

void testScheduledStopClearsOriginAndActiveOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.stop(scheduled));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmOrigin::None),
                        static_cast<int>(engine.origin()));
  TEST_ASSERT_EQUAL_INT64(0, engine.activeOccurrenceKey());
}

void testScheduledSnoozeCancellationResolvesOriginalOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.snooze(scheduled));
  TEST_ASSERT_TRUE(engine.stop(sample(27040, 2026, 9, 16, 258, 3, 7, 30)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmOrigin::None),
                        static_cast<int>(engine.origin()));
  TEST_ASSERT_EQUAL_INT64(0, engine.activeOccurrenceKey());
  TEST_ASSERT_EQUAL_INT64(0, engine.snoozeDeadline());
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.lastHandledOccurrenceKey());
  TEST_ASSERT_FALSE(engine.update(sample(27620, 2026, 9, 16, 258, 3, 7, 40)));
}

void testTestSnoozeCancellationDoesNotHandleOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto now = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  engine.testRing(now);
  TEST_ASSERT_TRUE(engine.snooze(now));
  TEST_ASSERT_TRUE(engine.stop(now));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Armed),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_EQUAL_INT64(0, engine.lastHandledOccurrenceKey());
}

void testNaturalSnoozeExpiryRetainsScheduledOccurrence() {
  alarmclock::AlarmEngine engine(enabledConfig());
  const auto scheduled = sample(27020, 2026, 9, 16, 258, 3, 7, 30);
  TEST_ASSERT_TRUE(engine.update(scheduled));
  TEST_ASSERT_TRUE(engine.snooze(scheduled));
  TEST_ASSERT_TRUE(engine.update(sample(27620, 2026, 9, 16, 258, 3, 7, 40)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmState::Ringing),
                        static_cast<int>(engine.state()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmOrigin::Scheduled),
                        static_cast<int>(engine.origin()));
  TEST_ASSERT_EQUAL_INT64(202609160730LL, engine.activeOccurrenceKey());
}

void testSerialParserCompatibility() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmSerialCommandType::ResetHandledDay),
                        static_cast<int>(alarmclock::parseAlarmSerialCommand("alarm reset-day").type));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(alarmclock::AlarmSerialCommandType::SetSnoozeMinutes),
                        static_cast<int>(alarmclock::parseAlarmSerialCommand("alarm snooze-min 10").type));
}

// ── Minute step and normalization tests ────────────────────────────
namespace steps {
constexpr uint8_t stepMinutePlus(uint8_t m) { return (m + 5) % 60; }
constexpr uint8_t stepMinuteMinus(uint8_t m) { return (m + 55) % 60; }
constexpr uint8_t stepHourPlus(uint8_t h) { return (h + 1) % 24; }
constexpr uint8_t stepHourMinus(uint8_t h) { return (h + 23) % 24; }
constexpr uint8_t normalizeMinute(uint8_t m) {
  return ((m + 2) / 5) * 5 >= 60 ? 0 : ((m + 2) / 5) * 5;
}
}  // namespace steps

void testMinutePlusStepsByFive() {
  TEST_ASSERT_EQUAL_UINT8(5, steps::stepMinutePlus(0));
  TEST_ASSERT_EQUAL_UINT8(10, steps::stepMinutePlus(5));
  TEST_ASSERT_EQUAL_UINT8(55, steps::stepMinutePlus(50));
  TEST_ASSERT_EQUAL_UINT8(0, steps::stepMinutePlus(55));
}

void testMinuteMinusStepsByFive() {
  TEST_ASSERT_EQUAL_UINT8(55, steps::stepMinuteMinus(0));
  TEST_ASSERT_EQUAL_UINT8(50, steps::stepMinuteMinus(55));
  TEST_ASSERT_EQUAL_UINT8(5, steps::stepMinuteMinus(10));
  TEST_ASSERT_EQUAL_UINT8(0, steps::stepMinuteMinus(5));
}

void testHourWrap() {
  TEST_ASSERT_EQUAL_UINT8(0, steps::stepHourPlus(23));
  TEST_ASSERT_EQUAL_UINT8(1, steps::stepHourPlus(0));
  TEST_ASSERT_EQUAL_UINT8(23, steps::stepHourMinus(0));
  TEST_ASSERT_EQUAL_UINT8(22, steps::stepHourMinus(23));
}

void testMinuteNormalization() {
  TEST_ASSERT_EQUAL_UINT8(0, steps::normalizeMinute(0));
  TEST_ASSERT_EQUAL_UINT8(0, steps::normalizeMinute(2));
  TEST_ASSERT_EQUAL_UINT8(5, steps::normalizeMinute(3));
  TEST_ASSERT_EQUAL_UINT8(5, steps::normalizeMinute(7));
  TEST_ASSERT_EQUAL_UINT8(10, steps::normalizeMinute(12));
  TEST_ASSERT_EQUAL_UINT8(25, steps::normalizeMinute(27));
  TEST_ASSERT_EQUAL_UINT8(55, steps::normalizeMinute(57));
  TEST_ASSERT_EQUAL_UINT8(0, steps::normalizeMinute(59));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testConfigDefaultsAndValidation);
  RUN_TEST(testOccurrenceKeyUsesLocalCivilDate);
  RUN_TEST(testSettingsOccurrenceRoundtrip64Bit);
  RUN_TEST(testLegacyDayKeyDoesNotBlockSchedule);
  RUN_TEST(testNormalOccurrenceRingsAtTwentySeconds);
  RUN_TEST(testStopHandlesOnlyExactOccurrence);
  RUN_TEST(testSameDayAlarmTimeChangeRings);
  RUN_TEST(testTestStopDoesNotPoisonRealSchedule);
  RUN_TEST(testTestSnoozeLifecycleDoesNotPoisonRealSchedule);
  RUN_TEST(testScheduledSnoozePreservesActiveOccurrence);
  RUN_TEST(testHardwareBlockedOccurrenceAndChangedTime);
  RUN_TEST(testNextDaySameTimeRings);
  RUN_TEST(testMatchingMinuteIsSecondIndependent);
  RUN_TEST(testWrongMinuteAndWeekdayDoNotRing);
  RUN_TEST(testSoftwareDisabledDoesNotConsumeOccurrence);
  RUN_TEST(testHardwareOffWhileRingingResolvesScheduledOnly);
  RUN_TEST(testPersistenceRestoreBlocksOnlySameOccurrence);
  RUN_TEST(testResetOccurrenceReenablesOnlyThatOccurrence);
  RUN_TEST(testTestRingUsesTestOriginAndNoActiveOccurrence);
  RUN_TEST(testSnoozeDeadlineUsesConfiguredMinutes);
  RUN_TEST(testHardwareOffUnrelatedMinuteDoesNotConsume);
  RUN_TEST(testChangingVolumeDoesNotClearHandledOccurrence);
  RUN_TEST(testInvalidSampleDoesNotTriggerSnoozedAlarm);
  RUN_TEST(testScheduledStopClearsOriginAndActiveOccurrence);
  RUN_TEST(testScheduledSnoozeCancellationResolvesOriginalOccurrence);
  RUN_TEST(testTestSnoozeCancellationDoesNotHandleOccurrence);
  RUN_TEST(testNaturalSnoozeExpiryRetainsScheduledOccurrence);
  RUN_TEST(testSerialParserCompatibility);
  RUN_TEST(testMinutePlusStepsByFive);
  RUN_TEST(testMinuteMinusStepsByFive);
  RUN_TEST(testHourWrap);
  RUN_TEST(testMinuteNormalization);
  return UNITY_END();
}
