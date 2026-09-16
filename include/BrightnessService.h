#pragma once

#include <Arduino.h>

class BrightnessService {
 public:
  void begin(uint8_t initialBrightness = 230);
  void update();
  void set(uint8_t brightness);
  void blankScreen();
  void wakeScreen();
  uint8_t value() const { return brightness_; }
  bool screenBlanked() const { return screenBlanked_; }
  uint16_t ambientRaw() const { return ambientRaw_; }
  void printStatus() const;

 private:
  static constexpr uint8_t kPwmChannel = 7;
  uint8_t brightness_ = 230;
  uint16_t ambientRaw_ = 0;
  uint32_t lastSampleAt_ = 0;
  bool hasAmbientSample_ = false;
  bool screenBlanked_ = false;
};
