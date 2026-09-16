#include "BrightnessService.h"

#include "hardware_pins.h"

void BrightnessService::begin(uint8_t initialBrightness) {
  pinMode(pins::kLightSensor, INPUT);
  ledcSetup(kPwmChannel, 5000, 8);
  ledcAttachPin(pins::kBacklight, kPwmChannel);
  set(initialBrightness);
}

void BrightnessService::update() {
  const uint32_t now = millis();
  if (hasAmbientSample_ && now - lastSampleAt_ < 1000) {
    return;
  }
  const uint16_t sample = analogRead(pins::kLightSensor);
  ambientRaw_ = hasAmbientSample_
                    ? static_cast<uint16_t>((ambientRaw_ * 7UL + sample) / 8)
                    : sample;
  hasAmbientSample_ = true;
  lastSampleAt_ = now;
}

void BrightnessService::set(uint8_t brightness) {
  brightness_ = brightness;
  if (!screenBlanked_) {
    ledcWrite(kPwmChannel, brightness_);
  }
}

void BrightnessService::blankScreen() {
  screenBlanked_ = true;
  ledcWrite(kPwmChannel, 0);
}

void BrightnessService::wakeScreen() {
  screenBlanked_ = false;
  ledcWrite(kPwmChannel, brightness_);
}

void BrightnessService::printStatus() const {
  Serial.printf("[BRIGHTNESS] value=%u ambient_raw=%u blanked=%s\n",
                static_cast<unsigned>(brightness_),
                static_cast<unsigned>(ambientRaw_),
                screenBlanked_ ? "yes" : "no");
}
