#pragma once

#include <Adafruit_SPITFT.h>
#include <Arduino.h>
#include <SD.h>

#include "EmojiLogic.h"

class EmojiService {
 public:
  EmojiService(Adafruit_SPITFT& display);

  // Draw an emoji from SD raw RGB565 data at the given position and size.
  // size is the target height/width (e.g. 32, 48).
  // Returns true if the emoji was found and rendered.
  bool draw(const char* utf8Emoji, int16_t x, int16_t y, uint8_t size);

 private:
  Adafruit_SPITFT& display_;
};