#include "EmojiService.h"

#include <SD.h>

#include <cstdio>

EmojiService::EmojiService(Adafruit_SPITFT& display) : display_(display) {
}

bool EmojiService::draw(const char* utf8Emoji, int16_t x, int16_t y,
                         uint8_t size) {
  char filename[emoji::kMaxFilenameBytes];
  if (!emoji::emojiToFilename(utf8Emoji, filename, sizeof(filename))) {
    return false;
  }

  char path[64];
  std::snprintf(path, sizeof(path), "/emoji/%u/%s.raw",
                static_cast<unsigned>(size), filename);

  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[EMOJI] missing %s\n", path);
    return false;
  }

  // Read raw RGB565 pixel data (width, height 16-bit LE header, then pixels).
  uint8_t header[4];
  if (file.read(header, 4) != 4) { file.close(); return false; }
  int16_t w = header[0] | (header[1] << 8);
  int16_t h = header[2] | (header[3] << 8);
  if (w < 1 || w > 48 || h < 1 || h > 48) { file.close(); return false; }

  const size_t pixelBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
  if (pixelBytes > 8192) { file.close(); return false; }

  uint8_t* buf = new (std::nothrow) uint8_t[pixelBytes];
  if (buf == nullptr) { file.close(); return false; }
  if (static_cast<size_t>(file.read(buf, pixelBytes)) != pixelBytes) {
    delete[] buf; file.close(); return false;
  }
  file.close();

  // Center in the requested size
  int16_t drawX = x + (static_cast<int16_t>(size) - w) / 2;
  int16_t drawY = y + (static_cast<int16_t>(size) - h) / 2;
  display_.drawRGBBitmap(drawX, drawY,
                          reinterpret_cast<uint16_t*>(buf), w, h);
  delete[] buf;
  return true;
}