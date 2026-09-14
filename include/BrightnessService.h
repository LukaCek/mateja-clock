#pragma once

#include <Arduino.h>

class BrightnessService {
 public:
  void begin(uint8_t initialBrightness = 230);
  void update();
  void set(uint8_t brightness);
  uint8_t value() const { return brightness_; }
  uint16_t ambientRaw() const { return ambientRaw_; }
  void printStatus() const;

 private:
  static constexpr uint8_t kPwmChannel = 7;
  uint8_t brightness_ = 230;
  uint16_t ambientRaw_ = 0;
  uint32_t lastSampleAt_ = 0;
  bool hasAmbientSample_ = false;
};
