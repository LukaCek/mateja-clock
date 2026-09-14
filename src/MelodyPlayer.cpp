#include "MelodyPlayer.h"

namespace {

constexpr uint8_t kLedgeResolution = 10;

}

void MelodyPlayer::start(const MelodyNote* melody, uint8_t noteCount,
                         uint8_t gpio, uint8_t ledcChannel) {
  stop();
  melody_ = melody;
  noteCount_ = noteCount;
  gpio_ = gpio;
  ledcChannel_ = ledcChannel;
  currentIndex_ = 0;
  playing_ = true;
  if (noteCount_ > 0 && melody_ != nullptr) {
    dacDisable(gpio_);
    pinMode(gpio_, OUTPUT);
    ledcSetup(ledcChannel_, melody_[0].frequencyHz, kLedgeResolution);
    ledcAttachPin(gpio_, ledcChannel_);
    playCurrentNote();
  }
}

void MelodyPlayer::update() {
  if (!playing_ || melody_ == nullptr || noteCount_ == 0) {
    return;
  }
  const uint32_t elapsed = millis() - noteStartedAt_;
  const uint16_t pauseAfterNote = 80;
  const uint32_t noteWindow =
      static_cast<uint32_t>(melody_[currentIndex_].durationMs) +
      pauseAfterNote;
  if (elapsed >= noteWindow) {
    advanceToNextNote();
  }
}

void MelodyPlayer::stop() {
  if (playing_) {
    ledcWriteTone(ledcChannel_, 0);
    ledcDetachPin(gpio_);
  }
  playing_ = false;
  melody_ = nullptr;
  noteCount_ = 0;
}

bool MelodyPlayer::isPlaying() const { return playing_; }

void MelodyPlayer::playCurrentNote() {
  const MelodyNote& note = melody_[currentIndex_];
  noteStartedAt_ = millis();
  ledcWriteTone(ledcChannel_, note.frequencyHz);
}

void MelodyPlayer::advanceToNextNote() {
  ++currentIndex_;
  if (currentIndex_ >= noteCount_) {
    currentIndex_ = 0;
  }
  playCurrentNote();
}
