#pragma once

#include <Arduino.h>

#include <cstdint>
#include <ctime>
#include <functional>

#include "TimeSource.h"

class TimeService {
 public:
  struct Snapshot {
    bool valid = false;
    time_t epochSeconds = 0;
    int year = 0;
    int yearDay = 0;
    int hour = 0;
    int minute = 0;
    int day = 0;
    int weekday = 0;
    int month = 0;
  };

  // Supplies a UTC epoch when the system/NTP clock is still unavailable
  // (e.g. the C3 DS1302 coprocessor after a fresh boot without Wi-Fi).
  using EpochProvider = std::function<std::int64_t()>;

  void begin(const char* ssid, const char* password);
  // Returns true when Home-visible time state changed.
  bool update();
  const Snapshot& snapshot() const { return snapshot_; }
  bool wifiConnected() const;
  // Local clock offset relative to UTC, in seconds (used for the message
  // time labels). 0 while the time is not valid.
  std::int32_t utcOffsetSeconds() const;
  // Which source is authoritative right now.
  timesource::Source source() const { return source_; }
  void printStatus() const;

  void setFallbackEpochProvider(EpochProvider provider);
  void clearFallbackEpochProvider();

 private:
  void startConnection();

  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  Snapshot snapshot_;
  EpochProvider fallbackProvider_;
  timesource::Source source_ = timesource::Source::kInvalid;
  uint32_t connectionStartedAt_ = 0;
  uint32_t lastConnectionAttemptAt_ = 0;
  uint32_t lastDisconnectLoggedAtMs_ = 0;
  uint32_t lastDisconnectReason_ = 0;
  time_t displayedMinute_ = 0;
  bool credentialsAvailable_ = false;
  bool ntpConfigured_ = false;
  bool connecting_ = false;
};