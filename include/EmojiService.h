#pragma once

#include <Adafruit_SPITFT.h>
#include <Arduino.h>
#include <PNGdec.h>
#include <SD.h>

#include "EmojiLogic.h"

class EmojiService {
 public:
  EmojiService(Adafruit_SPITFT& display);

  bool draw(const char* utf8Emoji, int16_t x, int16_t y, uint8_t size);

 private:
  static void pngDraw(PNGDRAW* draw);

  static EmojiService* activeInstance_;
  Adafruit_SPITFT& display_;
  int16_t drawX_ = 0;
  int16_t drawY_ = 0;
  uint8_t drawSize_ = 0;
};