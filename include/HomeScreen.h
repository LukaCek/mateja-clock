#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "PhotoService.h"
#include "StatusProviders.h"
#include "TimeService.h"

class HomeScreen {
 public:
  HomeScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text,
             PhotoService& photos, TimeService& time,
             const MessageStatusProvider& messages,
             const AlarmStatusProvider& alarm);

  void setPhotoStartResult(PhotoService::StartResult result);
  bool showInitial();
  bool refresh();
  bool nextPhoto();
  bool previousPhoto();
  bool randomPhoto();
  void showLibraryError();
  void refreshAlarm();
  void flashMessagesControl();
  void flashAlarmControl();
  uint32_t lastRenderMs() const { return lastRenderMs_; }

 private:
  bool finishPhotoRender(bool photoRendered, const char* reason);
  void drawFallback(const char* message, const char* detail = nullptr);
  void drawOverlay();
  void drawHeart();
  void drawMessages(bool pressed = false);
  void drawAlarm(bool pressed = false);
  void drawSnoozeIndicator();

  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  PhotoService& photos_;
  TimeService& time_;
  const MessageStatusProvider& messages_;
  const AlarmStatusProvider& alarm_;
  PhotoService::StartResult startResult_ =
      PhotoService::StartResult::kInvalidManifest;
  uint32_t lastRenderMs_ = 0;
};
