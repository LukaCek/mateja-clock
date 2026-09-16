#include <unity.h>

#include <cstdint>

#include "RtcLinkCore.h"
#include "RtclinkProtocol.h"
#include "TimeSource.h"

// ---------------------------------------------------------------------------
// TimeSource tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::int64_t kFloor = 1700000000;

void testTimeSourceNtpWhenAboveFloor() {
  TEST_ASSERT_EQUAL(timesource::Source::kNtp,
                    timesource::select(kFloor + 100, kFloor, false, 0));
  TEST_ASSERT_EQUAL(timesource::Source::kNtp,
                    timesource::select(kFloor + 100, kFloor, true, kFloor));
}

void testTimeSourceRtcWhenSystemInvalid() {
  TEST_ASSERT_EQUAL(timesource::Source::kRtc,
                    timesource::select(0, kFloor, true, kFloor + 100));
}

void testTimeSourceInvalidWhenBothUnavailable() {
  TEST_ASSERT_EQUAL(timesource::Source::kInvalid,
                    timesource::select(0, kFloor, false, 0));
}

void testTimeSourceInvalidWhenRtcEpochBelowFloor() {
  TEST_ASSERT_EQUAL(timesource::Source::kInvalid,
                    timesource::select(0, kFloor, true, 42));
}

void testTimeSourceResolveEpoch() {
  TEST_ASSERT_EQUAL(kFloor + 100,
                    timesource::resolveEpoch(timesource::Source::kNtp,
                                             kFloor + 100, 999));
  TEST_ASSERT_EQUAL(999,
                    timesource::resolveEpoch(timesource::Source::kRtc,
                                             kFloor + 100, 999));
  TEST_ASSERT_EQUAL(0,
                    timesource::resolveEpoch(timesource::Source::kInvalid,
                                             kFloor + 100, 999));
}

// ---------------------------------------------------------------------------
// RtcLinkCore tests
// ---------------------------------------------------------------------------

// Helper: push a complete encoded frame byte-by-byte into the parser.
void pushEncodedFrame(RtcLinkCore& core, const std::uint8_t* data,
                      std::uint8_t length, std::int64_t nowMs) {
  for (std::uint8_t i = 0; i < length; ++i) {
    core.pushByte(data[i], nowMs);
  }
}

void pushRtcTime(RtcLinkCore& core, std::uint32_t epoch, std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeRtcTime(epoch, buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void pushAlarmSwitch(RtcLinkCore& core, bool on, std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeAlarmSwitch(on, buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void pushVolumeChanged(RtcLinkCore& core, std::uint8_t volume,
                       std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeVolumeChanged(volume, buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void pushSnoozePressed(RtcLinkCore& core, std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeSnoozePressed(buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void pushStatus(RtcLinkCore& core, const rtclink::Status& status,
                std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeStatus(status, buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void pushSetRtcResult(RtcLinkCore& core, std::uint8_t result,
                      std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodeSetRtcResult(result, buffer, sizeof(buffer));
  pushEncodedFrame(core, buffer, len, nowMs);
}

void testRtcTimeUpdatesState() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 5000;
  pushRtcTime(core, 1789482605, kNowMs);
  TEST_ASSERT_TRUE(core.rtcValid());
  TEST_ASSERT_EQUAL_UINT32(1789482605, core.rtcEpoch());
  TEST_ASSERT_EQUAL_UINT32(1, core.framesAccepted());
  TEST_ASSERT_EQUAL_UINT32(1, core.framesValid());
}

void testSwitchUpdatesState() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 5000;
  pushAlarmSwitch(core, true, kNowMs);
  TEST_ASSERT_TRUE(core.alarmSwitchKnown());
  TEST_ASSERT_TRUE(core.alarmSwitchOn());

  pushAlarmSwitch(core, false, kNowMs + 100);
  TEST_ASSERT_TRUE(core.alarmSwitchKnown());
  TEST_ASSERT_FALSE(core.alarmSwitchOn());
}

void testVolumeUpdatesState() {
  RtcLinkCore core;
  pushVolumeChanged(core, 42, 1000);
  TEST_ASSERT_TRUE(core.volumeKnown());
  TEST_ASSERT_EQUAL_UINT8(42, core.volumePercent());
}

void testSnoozeConsumedOnce() {
  RtcLinkCore core;
  pushSnoozePressed(core, 1000);
  pushSnoozePressed(core, 1001);
  TEST_ASSERT_EQUAL_UINT32(2, core.consumeSnoozePressed());
  TEST_ASSERT_EQUAL_UINT32(0, core.consumeSnoozePressed());
}

void testStatusFrameSetsAllState() {
  RtcLinkCore core;
  rtclink::Status status;
  status.protocolVersion = rtclink::kProtocolVersion;
  status.rtcValid = true;
  status.rtcEpoch = 1789482605;
  status.alarmSwitchOn = true;
  status.volume = 77;
  status.uptimeSeconds = 12345;
  pushStatus(core, status, 1000);
  TEST_ASSERT_TRUE(core.rtcValid());
  TEST_ASSERT_EQUAL_UINT32(1789482605, core.rtcEpoch());
  TEST_ASSERT_TRUE(core.alarmSwitchOn());
  TEST_ASSERT_EQUAL_UINT8(77, core.volumePercent());
  TEST_ASSERT_TRUE(core.protocolCompatible());
  TEST_ASSERT_EQUAL_UINT32(1, core.framesAccepted());
}

void testStatusLowProtocolVersionSetsIncompatible() {
  RtcLinkCore core;
  rtclink::Status status;
  status.protocolVersion = 0;  // mismatch
  status.rtcValid = false;
  status.rtcEpoch = 0;
  status.alarmSwitchOn = false;
  status.volume = 0;
  status.uptimeSeconds = 0;
  pushStatus(core, status, 1000);
  TEST_ASSERT_FALSE(core.protocolCompatible());
}

void testCrcErrorCountsAsRejected() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 5000;
  // Build a valid frame then corrupt one CRC byte.
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t len =
      rtclink::encodePing(buffer, sizeof(buffer));
  buffer[len - 1] ^= 0xFF;  // corrupt CRC high byte.
  pushEncodedFrame(core, buffer, len, kNowMs);
  TEST_ASSERT_EQUAL_UINT32(1, core.crcErrors());
  TEST_ASSERT_EQUAL_UINT32(1, core.framesRejected());
}

void testVersionMismatchCounts() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 5000;
  // Send a single byte with wrong version (not kProtocolVersion).
  core.pushByte(rtclink::kSof, kNowMs);
  core.pushByte(rtclink::kProtocolVersion + 1, kNowMs);
  TEST_ASSERT_EQUAL_UINT32(1, core.versionMismatches());
  TEST_ASSERT_EQUAL_UINT32(1, core.framesRejected());
  TEST_ASSERT_FALSE(core.protocolCompatible());
}

void testConnectionTimeout() {
  RtcLinkCore core;
  pushRtcTime(core, 1789482605, 1000);
  TEST_ASSERT_TRUE(core.connected(1000));
  TEST_ASSERT_TRUE(core.connected(1000 + RtcLinkCore::kLinkTimeoutMs - 1));
  TEST_ASSERT_FALSE(core.connected(1000 + RtcLinkCore::kLinkTimeoutMs));
}

void testFirstTickEmitsStatus() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 100;
  core.onTick(kNowMs);
  TEST_ASSERT_TRUE(core.hasOutFrame());
  // The emitted frame should be a REQUEST_STATUS.
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(rtclink::Type::kRequestStatus),
                          core.outData()[2]);
  core.clearOutFrame();
  TEST_ASSERT_FALSE(core.hasOutFrame());
}

void testPeriodicPing() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 100;
  // First tick emits status.
  core.onTick(kNowMs);
  core.clearOutFrame();
  // After the first tick, at least 3s of silence should emit a PING.
  core.onTick(kNowMs + RtcLinkCore::kPingIntervalMs);
  TEST_ASSERT_TRUE(core.hasOutFrame());
  TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(rtclink::Type::kPing),
                          core.outData()[2]);
  core.clearOutFrame();
}

void testForceSyncQueuesSetRtcTime() {
  RtcLinkCore core;
  constexpr std::int64_t kNowMs = 100;
  core.onTick(kNowMs);       // status emitted; clear
  core.clearOutFrame();       // drain the initial status
  TEST_ASSERT_TRUE(core.forceSync(1789482605, kNowMs));
  TEST_ASSERT_TRUE(core.hasOutFrame());
  TEST_ASSERT_EQUAL_HEX8(
      static_cast<std::uint8_t>(rtclink::Type::kSetRtcTime),
      core.outData()[2]);
  TEST_ASSERT_EQUAL_UINT32(1, core.rtcWritesSent());
}

void testSynchronizeFirstEvaluation() {
  RtcLinkCore core;
  // Cannot sync before the link has ever been seen (coverage overlaps with
  // testSynchronizeSkipsWhenNotConnected).
  pushRtcTime(core, kFloor, 1000);  // valid but stale DS1302 value
  core.synchronize(kFloor + 100, 2000);  // drift 100s >= 30s threshold
  TEST_ASSERT_TRUE(core.hasOutFrame());
  // Should be SET_RTC_TIME with epoch = kFloor + 100.
  TEST_ASSERT_EQUAL_HEX8(
      static_cast<std::uint8_t>(rtclink::Type::kSetRtcTime),
      core.outData()[2]);
}

void testSynchronizeSkipsWhenDriftBelowThreshold() {
  RtcLinkCore core;
  pushRtcTime(core, kFloor + 200, 1000);  // DS1302 is valid, within threshold
  core.synchronize(kFloor + 210, 1000);   // drift = 10 < 30 threshold
  TEST_ASSERT_FALSE(core.hasOutFrame());   // no write
}

void testSynchronizeSkipsWhenNotConnected() {
  RtcLinkCore core;
  core.synchronize(kFloor + 100, 1000);
  TEST_ASSERT_FALSE(core.hasOutFrame());
}

void testSetRtcResultClearsPending() {
  RtcLinkCore core;
  TEST_ASSERT_TRUE(core.forceSync(1789482605, 1000));
  // Force an out frame; onTick won't send another while out_ is occupied.
  TEST_ASSERT_TRUE(core.hasOutFrame());
  // Simulate C3 response.
  pushSetRtcResult(core, 0, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, core.setRtcResultsSeen());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  // TimeSource
  RUN_TEST(testTimeSourceNtpWhenAboveFloor);
  RUN_TEST(testTimeSourceRtcWhenSystemInvalid);
  RUN_TEST(testTimeSourceInvalidWhenBothUnavailable);
  RUN_TEST(testTimeSourceInvalidWhenRtcEpochBelowFloor);
  RUN_TEST(testTimeSourceResolveEpoch);
  // RtcLinkCore
  RUN_TEST(testRtcTimeUpdatesState);
  RUN_TEST(testSwitchUpdatesState);
  RUN_TEST(testVolumeUpdatesState);
  RUN_TEST(testSnoozeConsumedOnce);
  RUN_TEST(testStatusFrameSetsAllState);
  RUN_TEST(testStatusLowProtocolVersionSetsIncompatible);
  RUN_TEST(testCrcErrorCountsAsRejected);
  RUN_TEST(testVersionMismatchCounts);
  RUN_TEST(testConnectionTimeout);
  RUN_TEST(testFirstTickEmitsStatus);
  RUN_TEST(testPeriodicPing);
  RUN_TEST(testForceSyncQueuesSetRtcTime);
  RUN_TEST(testSynchronizeFirstEvaluation);
  RUN_TEST(testSynchronizeSkipsWhenDriftBelowThreshold);
  RUN_TEST(testSynchronizeSkipsWhenNotConnected);
  RUN_TEST(testSetRtcResultClearsPending);
  return UNITY_END();
}