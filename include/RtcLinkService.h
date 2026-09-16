#pragma once

#include <Arduino.h>

#include <cstdint>

#include "RtcLinkCore.h"

// Thin Arduino adapter over RtcLinkCore. Owns the dedicated Serial1 UART to
// the ESP32-C3 DS1302 coprocessor: drains RX bytes into the core, flushes
// outgoing frames, and relays the pure link/state logic to the app. All
// protocol, heartbeat, and resync policy lives in RtcLinkCore (native-tested).
class RtcLinkService {
 public:
  void begin(HardwareSerial& link);

  // Drain RX, run the core timers, and flush any pending outgoing frame.
  // Call every main-loop iteration.
  void update();

  // Whether the link has exchanged frames recently (heartbeat alive).
  bool connected() const;
  bool protocolCompatible() const;
  std::int64_t lastSeenMs() const;

  // Latest decoded physical state from the C3.
  bool rtcValid() const;
  std::uint32_t rtcEpoch() const;
  bool alarmSwitchKnown() const;
  bool alarmSwitchOn() const;
  bool volumeKnown() const;
  std::uint8_t volumePercent() const;
  // Number of snooze presses observed since the last call.
  std::uint32_t consumeSnoozePressed();

  // NTP-driven DS1302 resync; has its own conservative write policy.
  void synchronize(std::int64_t trustedEpoch);
  // Unconditional resync for maintenance.
  bool forceSync(std::int64_t trustedEpoch);
  // Maintenance diagnostics.
  void requestStatus();
  void printStatus() const;

 private:
  HardwareSerial* link_ = nullptr;
  RtcLinkCore core_;
};