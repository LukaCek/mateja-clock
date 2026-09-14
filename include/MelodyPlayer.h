#pragma once

#include <Arduino.h>

struct MelodyNote {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

class MelodyPlayer {
 public:
  void start(const MelodyNote* melody, uint8_t noteCount, uint8_t gpio,
             uint8_t ledcChannel);
  void update();
  void stop();
  bool isPlaying() const;

 private:
  void playCurrentNote();
  void advanceToNextNote();

  const MelodyNote* melody_ = nullptr;
  uint8_t noteCount_ = 0;
  uint8_t gpio_ = 0;
  uint8_t ledcChannel_ = 0;
  uint8_t currentIndex_ = 0;
  uint32_t noteStartedAt_ = 0;
  bool playing_ = false;
};
