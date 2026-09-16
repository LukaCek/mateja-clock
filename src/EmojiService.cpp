#include "EmojiService.h"

#include <PNGdec.h>
#include <SD.h>

#include <cstdio>

EmojiService* EmojiService::activeInstance_ = nullptr;

EmojiService::EmojiService(Adafruit_SPITFT& display) : display_(display) {
  activeInstance_ = this;
}

bool EmojiService::draw(const char* utf8Emoji, int16_t x, int16_t y,
                         uint8_t size) {
  char filename[emoji::kMaxFilenameBytes];
  if (!emoji::emojiToFilename(utf8Emoji, filename, sizeof(filename))) {
    return false;
  }

  char path[64];
  std::snprintf(path, sizeof(path), "/emoji/%u/%s.png",
                static_cast<unsigned>(size), filename);

  if (!SD.exists(path)) {
    Serial.printf("[EMOJI] missing %s\n", path);
    return false;
  }

  drawX_ = x;
  drawY_ = y;
  drawSize_ = size;

  // Read the entire PNG file into RAM (small emoji assets, max ~5KB).
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize > 8192) {  // safety limit
    file.close();
    return false;
  }
  uint8_t* buf = new uint8_t[fileSize];
  if (buf == nullptr) {
    file.close();
    return false;
  }
  if (file.read(buf, fileSize) != fileSize) {
    delete[] buf;
    file.close();
    return false;
  }
  file.close();

  PNG png;
  int rc = png.openRAM(buf, fileSize, pngDraw);
  delete[] buf;
  if (rc != PNG_SUCCESS) {
    png.close();
    return false;
  }
  rc = png.decode(nullptr, 0);
  png.close();
  return rc == PNG_SUCCESS;
}

void EmojiService::pngDraw(PNGDRAW* draw) {
  if (activeInstance_ == nullptr || draw == nullptr) return;
  if (draw->y >= activeInstance_->drawSize_) return;

  EmojiService& self = *activeInstance_;
  const int16_t xOff = self.drawX_ + (self.drawSize_ - draw->iWidth) / 2;
  const int16_t rowY = self.drawY_ + draw->y;

  if (draw->iPixelType == 6) {
    // RGBA truecolor: 4 bytes per pixel (R,G,B,A)
    for (int16_t px = 0; px < draw->iWidth && px < self.drawSize_; ++px) {
      const uint8_t* pixel = draw->pPixels + px * 4;
      uint8_t a = pixel[3];
      // Only draw if at least 50% opaque
      if (a >= 128) {
        uint16_t rgb565 = ((pixel[0] & 0xF8) << 8) |
                           ((pixel[1] & 0xFC) << 3) |
                           (pixel[2] >> 3);
        self.display_.drawPixel(xOff + px, rowY, rgb565);
      }
    }
  } else if (draw->iPixelType == 2) {
    // RGB truecolor: 3 bytes per pixel (R,G,B)
    for (int16_t px = 0; px < draw->iWidth && px < self.drawSize_; ++px) {
      const uint8_t* pixel = draw->pPixels + px * 3;
      uint16_t rgb565 = ((pixel[0] & 0xF8) << 8) |
                         ((pixel[1] & 0xFC) << 3) |
                         (pixel[2] >> 3);
      self.display_.drawPixel(xOff + px, rowY, rgb565);
    }
  } else if (draw->iPixelType == 3) {
    // Indexed (palette)
    for (int16_t px = 0; px < draw->iWidth && px < self.drawSize_; ++px) {
      uint8_t idx = draw->pPixels[px];
      // Palette is 256 entries of 4 bytes (RGBx)
      const uint8_t* pal = draw->pPalette + idx * 4;
      uint16_t rgb565 = ((pal[0] & 0xF8) << 8) |
                         ((pal[1] & 0xFC) << 3) |
                         (pal[2] >> 3);
      self.display_.drawPixel(xOff + px, rowY, rgb565);
    }
  } else if (draw->iPixelType == 4) {
    // Grayscale with alpha: 2 bytes per pixel (G,A)
    for (int16_t px = 0; px < draw->iWidth && px < self.drawSize_; ++px) {
      const uint8_t* pixel = draw->pPixels + px * 2;
      if (pixel[1] >= 128) {
        uint8_t gray = pixel[0];
        uint16_t rgb565 = ((gray & 0xF8) << 8) | ((gray & 0xFC) << 3) | (gray >> 3);
        self.display_.drawPixel(xOff + px, rowY, rgb565);
      }
    }
  }
}