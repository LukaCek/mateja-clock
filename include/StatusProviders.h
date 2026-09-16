#pragma once

#include <Arduino.h>

#include "HomeLogic.h"

struct MessageStatus {
  uint16_t unreadCount = 0;
};

class MessageStatusProvider {
 public:
  virtual ~MessageStatusProvider() = default;
  virtual MessageStatus status() const = 0;
};

class AlarmStatusProvider {
 public:
  virtual ~AlarmStatusProvider() = default;
  virtual home::AlarmStatus status() const = 0;
  virtual bool snoozeActive() const { return false; }
};

class DemoMessageStatusProvider final : public MessageStatusProvider {
 public:
  MessageStatus status() const override { return status_; }
  void setUnreadCount(uint16_t count) { status_.unreadCount = count; }

 private:
  MessageStatus status_;
};

class DemoAlarmStatusProvider final : public AlarmStatusProvider {
 public:
  home::AlarmStatus status() const override { return status_; }
  bool applyCommand(const char* command) {
    return home::parseAlarmCommand(command, status_);
  }

 private:
  home::AlarmStatus status_;
};
