#include "AlarmAudio.h"

#include <SD.h>
#include <driver/i2s.h>

#include <cstring>

#include "hardware_pins.h"

namespace {

constexpr char kAlarmPath[] = "/clock/audio/alarm.wav";
constexpr uint8_t kToneChannel = 0;

const MelodyNote kLullaby[] = {
    {262, 380}, {262, 380}, {392, 380}, {392, 380},
    {440, 380}, {440, 380}, {392, 700}, {0, 200},
    {349, 380}, {349, 380}, {330, 380}, {330, 380},
    {294, 380}, {294, 380}, {262, 700},
};

uint16_t readAudio16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1]) << 8;
}

uint32_t readAudio32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         static_cast<uint32_t>(value[1]) << 8 |
         static_cast<uint32_t>(value[2]) << 16 |
         static_cast<uint32_t>(value[3]) << 24;
}

bool wavIsPlayable(const char* path) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  uint8_t riff[12];
  if (file.read(riff, sizeof(riff)) != sizeof(riff) ||
      memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    file.close();
    return false;
  }
  bool valid = false;
  while (file.available() >= 8) {
    uint8_t chunk[8];
    if (file.read(chunk, sizeof(chunk)) != sizeof(chunk)) {
      break;
    }
    const uint32_t size = readAudio32(chunk + 4);
    const uint32_t start = file.position();
    if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
      uint8_t details[16];
      if (file.read(details, sizeof(details)) != sizeof(details)) {
        break;
      }
      if (readAudio16(details) != 1 ||
          readAudio16(details + 2) != 1 || readAudio32(details + 4) != 22050 ||
          readAudio16(details + 14) != 16) {
        break;
      }
    } else if (memcmp(chunk, "data", 4) == 0) {
      valid = size > 0;
    }
    const uint32_t next = start + size + (size & 1U);
    if (next > file.size() || !file.seek(next)) {
      break;
    }
  }
  file.close();
  return valid;
}

}

bool AlarmAudio::begin() {
  audio_.forceMono(true);
  audio_.setVolume(libraryVolume());
  return true;
}

void AlarmAudio::start(uint8_t volume) {
  melodyPlayer_.stop();
  if (external_) {
    audio_.stopSong();
  }
  playing_ = false;
  external_ = false;
  setVolume(volume);
  dacDisable(pins::kAudio);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
  playing_ = true;
  if (!SD.exists(kAlarmPath) || !wavIsPlayable(kAlarmPath) ||
      !audio_.connecttoFS(SD, kAlarmPath)) {
    useBuiltIn("missing-or-invalid-wav");
    return;
  }
  external_ = true;
  sourceStatus_ = "esp32-audioi2s-wav";
  fallbackReason_ = "none";
  Serial.printf("[ALARM_AUDIO] WAV started path=%s volume=%u library=%u\n",
                kAlarmPath, static_cast<unsigned>(volume_),
                static_cast<unsigned>(libraryVolume()));
}

void AlarmAudio::setVolume(uint8_t volume) {
  volume_ = min<uint8_t>(volume, 100);
  audio_.setVolume(libraryVolume());
}

void AlarmAudio::update() {
  if (!playing_) {
    return;
  }
  if (external_) {
    audio_.loop();
    if (!audio_.isRunning()) {
      if (!audio_.connecttoFS(SD, kAlarmPath)) {
        useBuiltIn("wav-restart-failed");
      }
    }
  } else {
    melodyPlayer_.update();
  }
}

void AlarmAudio::stop() {
  melodyPlayer_.stop();
  if (external_) {
    audio_.stopSong();
  }
  playing_ = false;
  external_ = false;
  dacDisable(pins::kAudio);
  pinMode(pins::kAudio, OUTPUT);
  digitalWrite(pins::kAudio, LOW);
  sourceStatus_ = "stopped";
}

bool AlarmAudio::isPlaying() const { return playing_; }

bool AlarmAudio::usingExternalWav() const { return playing_ && external_; }

void AlarmAudio::printStatus() const {
  Serial.printf(
      "[ALARM_AUDIO] playing=%s source=%s volume=%u library=%u path=%s "
      "fallback=%s gpio=26\n",
      playing_ ? "yes" : "no", sourceStatus_,
      static_cast<unsigned>(volume_), static_cast<unsigned>(libraryVolume()),
      kAlarmPath, fallbackReason_);
}

void AlarmAudio::useBuiltIn(const char* reason) {
  if (external_) {
    audio_.stopSong();
  }
  external_ = false;
  fallbackReason_ = reason;
  sourceStatus_ = "lullaby-melody";
  dacDisable(pins::kAudio);
  melodyPlayer_.start(kLullaby,
                      static_cast<uint8_t>(sizeof(kLullaby) / sizeof(kLullaby[0])),
                      pins::kAudio, kToneChannel);
  Serial.printf("[ALARM_AUDIO] fallback melody reason=%s\n", reason);
}

uint8_t AlarmAudio::libraryVolume() const {
  if (volume_ == 0) {
    return 0;
  }
  return static_cast<uint8_t>(1 + volume_ * 11U / 100U);
}
