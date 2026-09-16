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
  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  alarmclock::AlarmConfig edit_;
  bool saveError_;
  bool hardwareAllowed_;
};

class RingingScreen {
 public:
  enum class Action { None, Snooze, Stop };

  RingingScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text);

  void draw(uint8_t hour, uint8_t minute, uint8_t snoozeMinutes,
            bool photoAvailable);
  Action handleTap(int16_t x, int16_t y) const;

  static AlarmScreenRect snoozeTarget();
  static AlarmScreenRect stopTarget();

 private:
  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
};
