#pragma once

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "AlarmLogic.h"

struct AlarmScreenRect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;

  bool contains(int16_t pointX, int16_t pointY) const;
};

class AlarmSettingsScreen {
 public:
  enum class Action { None, Save, Cancel };

  AlarmSettingsScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text);

  void beginEdit(const alarmclock::AlarmConfig& config);
  void draw();
  Action handleTap(int16_t x, int16_t y);
  void handleRepeat(bool touchActive, int16_t x, int16_t y, uint32_t nowMs);
  void checkRepeatStart(int16_t x, int16_t y, uint32_t nowMs);
  void startRepeatTracking(uint32_t nowMs);
  const alarmclock::AlarmConfig& editConfig() const;
  alarmclock::AlarmConfig& editConfig();
  void setSaveError(bool error);
  void setHardwareAllowed(bool allowed);

  static AlarmScreenRect backTarget();
  static AlarmScreenRect enabledTarget();
  static AlarmScreenRect hourMinusTarget();
  static AlarmScreenRect hourPlusTarget();
  static AlarmScreenRect minuteMinusTarget();
  static AlarmScreenRect minutePlusTarget();
  static AlarmScreenRect dayTarget(uint8_t day);
  static AlarmScreenRect saveTarget();

 private:
  enum class RepeatTarget : uint8_t { None, HourMinus, HourPlus, MinuteMinus, MinutePlus };

  void applyRepeat();
  void normalizeMinute();

  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  alarmclock::AlarmConfig edit_;
  bool saveError_;
  bool hardwareAllowed_;
  RepeatTarget repeatTarget_ = RepeatTarget::None;
  uint32_t repeatStartMs_ = 0;
  uint32_t lastRepeatMs_ = 0;
};

class RingingScreen {
 public:
  enum class Action { None, Snooze, Stop };

  RingingScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text);

  void draw(bool timeValid, int currentHour, int currentMinute,
            uint8_t snoozeMinutes, bool photoAvailable);
  void refreshCurrentTime(bool timeValid, int currentHour, int currentMinute);
  Action handleTap(int16_t x, int16_t y) const;

  static AlarmScreenRect snoozeTarget();
  static AlarmScreenRect stopTarget();

 private:
  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  bool timeKnown_ = false;
  bool displayedTimeValid_ = false;
  int displayedHour_ = 0;
  int displayedMinute_ = 0;
};
