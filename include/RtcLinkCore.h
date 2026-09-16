#pragma once

#include <cstdint>

#include "RtclinkProtocol.h"

// Pure, Arduino-free protocol + link-state logic for the C3 UART link. Owns
// the incremental frame parser, the PING heartbeat, the connection timeout,
// the decoded physical state (DS1302 time, alarm switch, volume, snooze), and
// the conservative NTP->DS1302 resync policy. Host-testable on the native
// build; the Arduino RtcLinkService is a thin adapter that feeds bytes and a
// monotonic millisecond clock in and relays emitted frames out.
//
// A signed 64-bit clock value (the caller's millis()) keeps timing portable;
// the callers on device pass millis() and tests pass synthetic timestamps.
class RtcLinkCore {
 public:
  // Heartbeat / timeout timings (caller clock units).
  static constexpr std::uint32_t kPingIntervalMs = 3000;
  static constexpr std::uint32_t kStatusIntervalMs = 30000;
  static constexpr std::uint32_t kLinkTimeoutMs = 12000;
  static constexpr std::uint32_t kSetRtcTimeoutMs = 5000;
  // RTC resync policy: only rewrite the DS1302 when it differs from the
  // trusted NTP epoch by at least kSyncThresholdSec, and re-check at most
  // every kSyncIntervalMs.
  static constexpr std::int64_t kSyncThresholdSec = 30;
  static constexpr std::uint32_t kSyncIntervalMs = 3600000;
  static constexpr std::int64_t kMinimumEpoch = 1700000000;

  // Feed one received byte. nowMs must be the caller's monotonic clock.
  void pushByte(std::uint8_t byte, std::int64_t nowMs);

  // Advance timers and schedule outgoing heartbeat/status frames. Called every
  // update() tick with the current clock value.
  void onTick(std::int64_t nowMs);

  // True when an outgoing frame is ready to be flushed to the UART.
  bool hasOutFrame() const { return outLength_ != 0; }
  std::uint8_t outLength() const { return outLength_; }
  const std::uint8_t* outData() const { return out_; }
  void clearOutFrame() { outLength_ = 0; }

  // Outgoing commands.
  void sendPingNow(std::int64_t nowMs);          // fire a ping at the next tick
  void requestStatusNow(std::int64_t nowMs);     // fire STATUS at the next tick
  bool setRtcTime(std::uint32_t epoch, std::int64_t nowMs);
  // Conservative resync: writes only on first trusted evaluation or after
  // kSyncIntervalMs, and only when the DS1302 is invalid or drifts past
  // kSyncThresholdSec. Ignores the request while the link is down.
  void synchronize(std::int64_t trustedEpoch, std::int64_t nowMs);
  // Unconditional write (serial `!rtc sync` / maintenance). Respects pending
  // in-flight frames.
  bool forceSync(std::int64_t trustedEpoch, std::int64_t nowMs);

  // Connection / protocol state.
  bool connected(std::int64_t nowMs) const;
  bool protocolCompatible() const { return compatible_; }
  std::int64_t lastSeenMs() const { return lastSeenAtMs_; }

  // Received physical state.
  bool rtcValid() const { return rtcValid_; }
  std::uint32_t rtcEpoch() const { return rtcEpoch_; }
  bool alarmSwitchKnown() const { return switchKnown_; }
  bool alarmSwitchOn() const { return switchOn_; }
  bool volumeKnown() const { return volumeKnown_; }
  std::uint8_t volumePercent() const { return volume_; }

  // Returns how many SNOOZE_PRESSED events have arrived since the last call.
  std::uint32_t consumeSnoozePressed();

  // Diagnostics counters.
  std::uint32_t framesValid() const { return framesValid_; }
  std::uint32_t framesAccepted() const { return framesAccepted_; }
  std::uint32_t framesRejected() const { return framesRejected_; }
  std::uint32_t crcErrors() const { return crcErrors_; }
  std::uint32_t unknownTypes() const { return unknownTypes_; }
  std::uint32_t versionMismatches() const { return versionMismatches_; }
  std::uint32_t tooLongFrames() const { return tooLongFrames_; }
  std::uint32_t pingsSent() const { return pingsSent_; }
  std::uint32_t statusesSent() const { return statusesSent_; }
  std::uint32_t rtcWritesSent() const { return rtcWritesSent_; }
  std::uint32_t setRtcResultsSeen() const { return setRtcResultsSeen_; }

 private:
  void handleParseStatus(rtclink::ParseStatus status, std::int64_t nowMs);
  void handleFrame(const rtclink::Frame& frame, std::int64_t nowMs);
  void emitStatus(std::int64_t nowMs);
  void emitPing(std::int64_t nowMs);
  bool queueFrame(const std::uint8_t* data, std::uint8_t length);

  rtclink::FrameParser parser_;

  std::uint8_t out_[rtclink::kMaxFrame];
  std::uint8_t outLength_ = 0;

  bool firstTick_ = true;
  bool wantStatusAtTick_ = false;
  std::int64_t lastPingAtMs_ = 0;
  std::int64_t lastStatusAtMs_ = 0;
  std::int64_t lastSeenAtMs_ = 0;
  bool wasConnected_ = false;

  bool rtcValid_ = false;
  std::uint32_t rtcEpoch_ = 0;
  bool switchKnown_ = false;
  bool switchOn_ = false;
  bool volumeKnown_ = false;
  std::uint8_t volume_ = 0;
  std::uint32_t snoozePending_ = 0;

  bool compatible_ = true;

  bool syncEvaluated_ = false;
  std::int64_t lastSyncCheckAtMs_ = 0;
  bool pendingSetRtc_ = false;
  std::int64_t setRtcQueuedAtMs_ = 0;

  std::uint32_t framesValid_ = 0;
  std::uint32_t framesAccepted_ = 0;
  std::uint32_t framesRejected_ = 0;
  std::uint32_t crcErrors_ = 0;
  std::uint32_t unknownTypes_ = 0;
  std::uint32_t versionMismatches_ = 0;
  std::uint32_t tooLongFrames_ = 0;
  std::uint32_t pingsSent_ = 0;
  std::uint32_t statusesSent_ = 0;
  std::uint32_t rtcWritesSent_ = 0;
  std::uint32_t setRtcResultsSeen_ = 0;
};