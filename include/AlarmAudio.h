#pragma once

#include <Arduino.h>
#include <Audio.h>

#include "MelodyPlayer.h"

class AlarmAudio {
 public:
  bool begin();
  void start(uint8_t volume);
  void setVolume(uint8_t volume);
  void update();
  void stop();
  bool isPlaying() const;
  bool usingExternalWav() const;
  void printStatus() const;

 private:
  void useBuiltIn(const char* reason);
  uint8_t libraryVolume() const;

  Audio audio_{true, I2S_DAC_CHANNEL_LEFT_EN};
  MelodyPlayer melodyPlayer_;
  const char* sourceStatus_ = "idle";
  const char* fallbackReason_ = "none";
  uint8_t volume_ = 65;
  bool playing_ = false;
  bool external_ = false;
};
