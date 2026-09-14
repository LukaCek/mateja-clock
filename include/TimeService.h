#pragma once

#include <Arduino.h>
#include <ctime>

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

  void begin(const char* ssid, const char* password);
  // Returns true when Home-visible time state changed.
  bool update();
  const Snapshot& snapshot() const { return snapshot_; }
  bool wifiConnected() const;
  void printStatus() const;

 private:
  void startConnection();

  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  Snapshot snapshot_;
  uint32_t connectionStartedAt_ = 0;
  uint32_t lastConnectionAttemptAt_ = 0;
  time_t displayedMinute_ = 0;
  bool credentialsAvailable_ = false;
  bool ntpConfigured_ = false;
  bool connecting_ = false;
};
