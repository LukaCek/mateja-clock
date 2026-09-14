#pragma once

#include <Adafruit_SPITFT.h>
#include <Arduino.h>
#include <SPI.h>

class PhotoService {
 public:
  enum class StartResult : uint8_t {
    kReady,
    kSdUnavailable,
    kInvalidManifest,
  };

  struct Stats {
    uint32_t successfulRenders = 0;
    uint32_t failedRenders = 0;
    uint32_t minimumFreeHeap = UINT32_MAX;
    uint32_t maximumRenderMs = 0;
    uint32_t lastRenderMs = 0;
    uint64_t totalRenderMs = 0;
  };

  PhotoService(Adafruit_SPITFT& display, SPIClass& sdSpi);

  StartResult begin();
  bool next();
  bool previous();
  bool random();
  bool renderCurrent();
  bool runFailureSelfTest();
  void printStats() const;
  void setBottomGradient(bool enabled) { bottomGradientEnabled_ = enabled; }

  uint16_t count() const { return photoCount_; }
  uint16_t currentNumber() const;
  const Stats& stats() const { return stats_; }
  uint32_t lastRenderMs() const { return stats_.lastRenderMs; }

 private:
  static constexpr uint16_t kNoPhoto = UINT16_MAX;

  static bool jpegBlock(int16_t x, int16_t y, uint16_t width,
                        uint16_t height, uint16_t* pixels);
  bool loadManifest(const char* path, bool apply);
  bool renderFrom(uint16_t firstIndex, int8_t direction);
  bool renderIndex(uint16_t index);
  void recordHeap();

  static PhotoService* activeInstance_;
  Adafruit_SPITFT& display_;
  SPIClass& sdSpi_;
  uint16_t photoCount_ = 0;
  uint16_t currentIndex_ = kNoPhoto;
  Stats stats_;
  bool bottomGradientEnabled_ = false;
};
