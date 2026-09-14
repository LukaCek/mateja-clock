#include "PhotoService.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <TJpg_Decoder.h>

#include "hardware_pins.h"
#include "HomeLogic.h"

namespace {

constexpr char kManifestPath[] = "/clock/manifest.json";
constexpr uint32_t kSdFrequency = 10000000;
constexpr uint16_t kExpectedWidth = 320;
constexpr uint16_t kExpectedHeight = 240;

bool insideRoundedRect(int16_t px, int16_t py, int16_t x, int16_t y,
                       int16_t width, int16_t height, int16_t radius) {
  if (px < x || py < y || px >= x + width || py >= y + height) {
    return false;
  }
  int16_t cornerX = px;
  int16_t cornerY = py;
  if (px < x + radius) {
    cornerX = x + radius;
  } else if (px >= x + width - radius) {
    cornerX = x + width - radius - 1;
  }
  if (py < y + radius) {
    cornerY = y + radius;
  } else if (py >= y + height - radius) {
    cornerY = y + height - radius - 1;
  }
  const int16_t dx = px - cornerX;
  const int16_t dy = py - cornerY;
  return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

PhotoService* PhotoService::activeInstance_ = nullptr;

PhotoService::PhotoService(Adafruit_SPITFT& display, SPIClass& sdSpi)
    : display_(display), sdSpi_(sdSpi) {}

PhotoService::StartResult PhotoService::begin() {
  sdSpi_.begin(pins::kSdClock, pins::kSdMiso, pins::kSdMosi,
               pins::kSdChipSelect);
  if (!SD.begin(pins::kSdChipSelect, sdSpi_, kSdFrequency)) {
    Serial.println("[PHOTO] SD mount failed");
    return StartResult::kSdUnavailable;
  }
  Serial.printf("[PHOTO] SD mounted card_mb=%llu\n",
                SD.cardSize() / (1024ULL * 1024ULL));

  if (!loadManifest(kManifestPath, true)) {
    return StartResult::kInvalidManifest;
  }

  activeInstance_ = this;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpegBlock);
  recordHeap();
  return StartResult::kReady;
}

bool PhotoService::loadManifest(const char* path, bool apply) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[PHOTO] manifest open failed path=%s\n", path);
    return false;
  }

  StaticJsonDocument<64> filter;
  filter["version"] = true;
  filter["count"] = true;
  StaticJsonDocument<128> manifest;
  const DeserializationError error = deserializeJson(
      manifest, file, DeserializationOption::Filter(filter));
  file.close();

  if (error || manifest.overflowed() ||
      !manifest["version"].is<uint32_t>() ||
      !manifest["count"].is<uint32_t>()) {
    Serial.printf("[PHOTO] manifest parse/schema failed error=%s overflow=%s\n",
                  error.c_str(), manifest.overflowed() ? "yes" : "no");
    return false;
  }

  const uint32_t version = manifest["version"].as<uint32_t>();
  const uint32_t count = manifest["count"].as<uint32_t>();
  if (version != 1 || count == 0 || count > 9999) {
    Serial.printf("[PHOTO] manifest values invalid version=%u count=%u\n",
                  version, count);
    return false;
  }

  if (apply) {
    photoCount_ = static_cast<uint16_t>(count);
  }
  Serial.printf("[PHOTO] manifest ready path=%s count=%u\n", path,
                static_cast<unsigned>(count));
  return true;
}

bool PhotoService::jpegBlock(int16_t x, int16_t y, uint16_t width,
                             uint16_t height, uint16_t* pixels) {
  if (activeInstance_ == nullptr || x < 0 || y < 0 ||
      x + width > activeInstance_->display_.width() ||
      y + height > activeInstance_->display_.height()) {
    return false;
  }
  if (activeInstance_->bottomGradientEnabled_) {
    constexpr int16_t kGradientTop = 132;
    constexpr uint16_t kBottomScale = 82;
    constexpr uint16_t kScaleRange = 255 - kBottomScale;
    constexpr uint16_t kGradientHeight = kExpectedHeight - 1 - kGradientTop;
    for (uint16_t row = 0; row < height; ++row) {
      const int16_t screenY = y + row;
      if (screenY <= kGradientTop) {
        continue;
      }
      const uint16_t scale =
          255 - static_cast<uint16_t>(screenY - kGradientTop) * kScaleRange /
                    kGradientHeight;
      uint16_t* line = pixels + static_cast<uint32_t>(row) * width;
      for (uint16_t column = 0; column < width; ++column) {
        const uint16_t color = line[column];
        const uint16_t red = ((color >> 11) & 0x1F) * scale / 255;
        const uint16_t green = ((color >> 5) & 0x3F) * scale / 255;
        const uint16_t blue = (color & 0x1F) * scale / 255;
        line[column] = static_cast<uint16_t>((red << 11) | (green << 5) | blue);
      }
    }

    // Lightly blend the photo under the top controls so their outlined button
    // surfaces remain readable without opaque panels or a framebuffer.
    for (uint16_t row = 0; row < height; ++row) {
      const int16_t screenY = y + row;
      if (screenY > 36) {
        continue;
      }
      uint16_t* line = pixels + static_cast<uint32_t>(row) * width;
      for (uint16_t column = 0; column < width; ++column) {
        const int16_t screenX = x + column;
        if (!insideRoundedRect(screenX, screenY, 5, 5, 26, 27, 7) &&
            !insideRoundedRect(screenX, screenY, 280, 5, 32, 27, 7)) {
          continue;
        }
        constexpr uint16_t alpha = 60;
        const uint16_t color = line[column];
        const uint16_t red = (((color >> 11) & 0x1F) * (255 - alpha) +
                              31 * alpha) /
                             255;
        const uint16_t green = (((color >> 5) & 0x3F) * (255 - alpha) +
                                61 * alpha) /
                               255;
        const uint16_t blue = ((color & 0x1F) * (255 - alpha) + 27 * alpha) /
                              255;
        line[column] = static_cast<uint16_t>((red << 11) | (green << 5) | blue);
      }
    }
  }
  activeInstance_->display_.setAddrWindow(x, y, width, height);
  activeInstance_->display_.writePixels(pixels,
                                        static_cast<uint32_t>(width) * height);
  return true;
}

bool PhotoService::renderIndex(uint16_t index) {
  char path[48];
  snprintf(path, sizeof(path), "/clock/photos/photo_%04u.jpg", index + 1);

  if (!SD.exists(path)) {
    ++stats_.failedRenders;
    Serial.printf("[PHOTO] missing filename=%s\n", path);
    recordHeap();
    return false;
  }

  uint16_t width = 0;
  uint16_t height = 0;
  const JRESULT sizeResult = TJpgDec.getFsJpgSize(&width, &height, path, SD);
  if (sizeResult != JDR_OK || width != kExpectedWidth ||
      height != kExpectedHeight) {
    ++stats_.failedRenders;
    Serial.printf("[PHOTO] rejected filename=%s result=%u size=%ux%u\n", path,
                  static_cast<unsigned>(sizeResult), width, height);
    recordHeap();
    return false;
  }

  const uint32_t heapBefore = ESP.getFreeHeap();
  const uint32_t startedAt = millis();
  display_.startWrite();
  const JRESULT result = TJpgDec.drawFsJpg(0, 0, path, SD);
  display_.endWrite();
  const uint32_t elapsedMs = millis() - startedAt;
  const uint32_t heapAfter = ESP.getFreeHeap();

  if (result != JDR_OK) {
    ++stats_.failedRenders;
    Serial.printf(
        "[PHOTO] decode failed filename=%s result=%u render_ms=%u free_heap=%u\n",
        path, static_cast<unsigned>(result), elapsedMs, heapAfter);
    recordHeap();
    return false;
  }

  currentIndex_ = index;
  ++stats_.successfulRenders;
  stats_.lastRenderMs = elapsedMs;
  stats_.totalRenderMs += elapsedMs;
  stats_.maximumRenderMs = max(stats_.maximumRenderMs, elapsedMs);
  recordHeap();
  Serial.printf(
      "PHOTO filename=%s render_ms=%u heap_before=%u free_heap=%u delta=%ld\n",
      path, elapsedMs, heapBefore, heapAfter,
      static_cast<long>(heapAfter) - static_cast<long>(heapBefore));
  return true;
}

bool PhotoService::renderFrom(uint16_t firstIndex, int8_t direction) {
  if (photoCount_ == 0) {
    return false;
  }
  uint16_t index = firstIndex;
  for (uint16_t attempt = 0; attempt < photoCount_; ++attempt) {
    if (renderIndex(index)) {
      return true;
    }
    index = direction > 0 ? (index + 1) % photoCount_
                          : (index + photoCount_ - 1) % photoCount_;
  }
  Serial.println("[PHOTO] no valid JPEG assets found");
  return false;
}

bool PhotoService::next() {
  const uint16_t first = currentIndex_ == kNoPhoto
                             ? 0
                             : (currentIndex_ + 1) % photoCount_;
  return renderFrom(first, 1);
}

bool PhotoService::previous() {
  const uint16_t first = currentIndex_ == kNoPhoto
                             ? photoCount_ - 1
                             : (currentIndex_ + photoCount_ - 1) % photoCount_;
  return renderFrom(first, -1);
}

bool PhotoService::random() {
  if (photoCount_ == 0) {
    return false;
  }
  const uint16_t first = static_cast<uint16_t>(home::chooseRandomIndex(
      photoCount_, currentIndex_, esp_random()));
  return renderFrom(first, 1);
}

bool PhotoService::renderCurrent() {
  if (currentIndex_ == kNoPhoto) {
    return random();
  }
  if (renderIndex(currentIndex_)) {
    return true;
  }
  return renderFrom((currentIndex_ + 1) % photoCount_, 1);
}

bool PhotoService::runFailureSelfTest() {
  constexpr char kCorruptPath[] = "/clock/photos/photo_9999.jpg";
  constexpr char kBadManifestPath[] = "/clock/.bad_manifest.tmp";
  Serial.println("[PHOTO_TEST] missing/corrupt recovery starting");

  const bool missingSkipped = renderFrom(photoCount_, 1);

  if (SD.exists(kCorruptPath)) {
    Serial.println("[PHOTO_TEST] refusing to overwrite existing photo_9999.jpg");
    return false;
  }
  File corrupt = SD.open(kCorruptPath, FILE_WRITE);
  if (!corrupt) {
    Serial.println("[PHOTO_TEST] could not create temporary corrupt JPEG");
    return false;
  }
  constexpr uint8_t kInvalidJpeg[] = {0xFF, 0xD8, 0x00, 0x11, 0x22, 0x33};
  const size_t written = corrupt.write(kInvalidJpeg, sizeof(kInvalidJpeg));
  corrupt.close();
  if (written != sizeof(kInvalidJpeg)) {
    SD.remove(kCorruptPath);
    Serial.println("[PHOTO_TEST] temporary corrupt JPEG write failed");
    return false;
  }

  const bool corruptSkipped = renderFrom(9998, 1);
  const bool removed = SD.remove(kCorruptPath);

  if (SD.exists(kBadManifestPath)) {
    SD.remove(kBadManifestPath);
  }
  File badManifest = SD.open(kBadManifestPath, FILE_WRITE);
  bool manifestRejected = false;
  bool manifestRemoved = false;
  if (badManifest) {
    badManifest.print("{not valid json");
    badManifest.close();
    manifestRejected = !loadManifest(kBadManifestPath, false);
    manifestRemoved = SD.remove(kBadManifestPath);
  }

  const bool passed = missingSkipped && corruptSkipped && removed &&
                      manifestRejected && manifestRemoved;
  Serial.printf(
      "[PHOTO_TEST] failure recovery=%s corrupt_removed=%s "
      "bad_manifest_rejected=%s manifest_removed=%s free_heap=%u\n",
      passed ? "PASS" : "FAIL", removed ? "yes" : "no",
      manifestRejected ? "yes" : "no", manifestRemoved ? "yes" : "no",
      ESP.getFreeHeap());
  return passed;
}

uint16_t PhotoService::currentNumber() const {
  return currentIndex_ == kNoPhoto ? 0 : currentIndex_ + 1;
}

void PhotoService::recordHeap() {
  stats_.minimumFreeHeap = min(stats_.minimumFreeHeap, ESP.getFreeHeap());
}

void PhotoService::printStats() const {
  const uint32_t average = stats_.successfulRenders == 0
                               ? 0
                               : stats_.totalRenderMs / stats_.successfulRenders;
  Serial.printf(
      "[PHOTO_STATS] count=%u current=%u rendered=%u failed=%u avg_ms=%u "
      "max_ms=%u min_heap=%u free_heap=%u\n",
      photoCount_, currentNumber(), stats_.successfulRenders,
      stats_.failedRenders, average, stats_.maximumRenderMs,
      stats_.minimumFreeHeap, ESP.getFreeHeap());
}
