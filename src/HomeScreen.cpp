#include "HomeScreen.h"

#include "AlarmIcon.h"
#include "HomeLogic.h"

namespace {

constexpr uint16_t kFallbackBackground = 0x1082;
constexpr uint16_t kWarmWhite = 0xFF7B;
constexpr uint16_t kMuted = 0xC5F2;
constexpr uint16_t kCoral = 0xEBAC;
constexpr uint16_t kPressed = 0xFFE0;

}  // namespace

HomeScreen::HomeScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text,
                       PhotoService& photos, TimeService& time,
                       const MessageStatusProvider& messages,
                       const AlarmStatusProvider& alarm)
    : display_(display),
      text_(text),
      photos_(photos),
      time_(time),
      messages_(messages),
      alarm_(alarm) {}

void HomeScreen::setPhotoStartResult(PhotoService::StartResult result) {
  startResult_ = result;
}

bool HomeScreen::showInitial() {
  if (startResult_ == PhotoService::StartResult::kReady) {
    return finishPhotoRender(photos_.random(), "initial");
  }
  drawFallback(startResult_ == PhotoService::StartResult::kSdUnavailable
                   ? u8"SD kartica ni na voljo"
                   : u8"Napaka knjižnice slik",
               startResult_ == PhotoService::StartResult::kInvalidManifest
                   ? u8"Preveri kartico SD"
                   : nullptr);
  return false;
}

bool HomeScreen::refresh() {
  if (startResult_ != PhotoService::StartResult::kReady) {
    return showInitial();
  }
  return finishPhotoRender(photos_.renderCurrent(), "refresh");
}

bool HomeScreen::nextPhoto() {
  return finishPhotoRender(photos_.next(), "next");
}

bool HomeScreen::previousPhoto() {
  return finishPhotoRender(photos_.previous(), "previous");
}

bool HomeScreen::randomPhoto() {
  return finishPhotoRender(photos_.random(), "random");
}

bool HomeScreen::finishPhotoRender(bool photoRendered, const char* reason) {
  const uint32_t startedAt = millis();
  if (!photoRendered) {
    showLibraryError();
    return false;
  }
  drawOverlay();
  lastRenderMs_ = millis() - startedAt + photos_.lastRenderMs();
  Serial.printf("HOME reason=%s total_ms=%u photo_ms=%u free_heap=%u\n", reason,
                lastRenderMs_, photos_.lastRenderMs(), ESP.getFreeHeap());
  return true;
}

void HomeScreen::drawFallback(const char* message, const char* detail) {
  display_.fillScreen(kFallbackBackground);
  drawOverlay();
  text_.setFont(u8g2_font_9x15_te);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  const int16_t width = text_.getUTF8Width(message);
  text_.drawUTF8(max<int16_t>(8, (display_.width() - width) / 2), 126, message);
  if (detail != nullptr) {
    text_.setFont(u8g2_font_6x12_te);
    text_.setFontMode(1);
    text_.setForegroundColor(kMuted);
    const int16_t detailWidth = text_.getUTF8Width(detail);
    text_.drawUTF8(max<int16_t>(8, (display_.width() - detailWidth) / 2), 146,
                   detail);
  }
}

void HomeScreen::showLibraryError() {
  drawFallback(u8"Napaka knjižnice slik", u8"Preveri kartico SD");
}

void HomeScreen::drawOverlay() {
  drawHeart();
  drawMessages();

  char timeText[6] = "--:--";
  char dateText[48] = "";
  const TimeService::Snapshot& now = time_.snapshot();
  if (now.valid) {
    home::formatTime24(now.hour, now.minute, timeText, sizeof(timeText));
    home::formatSlovenianDate(now.weekday, now.day, now.month, dateText,
                             sizeof(dateText));
  }

  text_.setFont(u8g2_font_logisoso32_tf);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(12, 204, timeText);

  if (dateText[0] != '\0') {
    text_.setFont(u8g2_font_6x12_te);
    text_.setFontMode(1);
    text_.setForegroundColor(kWarmWhite);
    text_.drawUTF8(12, 229, dateText);
  }
  drawAlarm();
}

void HomeScreen::drawHeart() {
  display_.fillCircle(14, 14, 5, kCoral);
  display_.fillCircle(20, 14, 5, kCoral);
  display_.fillTriangle(9, 15, 25, 15, 17, 26, kCoral);
}

void HomeScreen::drawMessages(bool pressed) {
  const uint16_t color = pressed ? kPressed : kWarmWhite;
  display_.drawRoundRect(284, 10, 24, 17, 3, color);
  display_.drawLine(285, 11, 296, 20, color);
  display_.drawLine(307, 11, 296, 20, color);

  char badge[3];
  home::formatUnreadBadge(messages_.status().unreadCount, badge,
                          sizeof(badge));
  if (badge[0] == '\0') {
    return;
  }
  const uint8_t badgeWidth = badge[1] == '+' ? 19 : 14;
  const int16_t badgeX = 315 - badgeWidth;
  display_.fillRoundRect(badgeX, 1, badgeWidth, 13, 6, kCoral);
  text_.setFont(u8g2_font_5x8_tf);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(badgeX + (badgeWidth - text_.getUTF8Width(badge)) / 2, 11,
                 badge);
}

void HomeScreen::refreshAlarm() {
  drawAlarm();
}

void HomeScreen::drawAlarm(bool pressed) {
  const home::AlarmStatus status = alarm_.status();
  const bool configured = status.softwareEnabled;
  const bool hardwareBlocked = configured && !status.hardwareAllowed;
  // A configured-but-hardware-blocked alarm renders muted; a fully armed
  // alarm uses the coral accent.
  const uint16_t accent =
      pressed ? kPressed : (configured && !hardwareBlocked ? kCoral : kMuted);
  if (configured) {
    char alarmTime[6];
    home::formatTime24(status.hour, status.minute, alarmTime,
                       sizeof(alarmTime));
    text_.setFont(u8g2_font_6x12_tf);
    text_.setFontMode(1);
    text_.setForegroundColor(hardwareBlocked ? kMuted : kWarmWhite);
    text_.drawUTF8(243, 229, alarmTime);
  }

  display_.drawXBitmap(284, 207, kAlarmIcon, 24, 24, accent);
}

void HomeScreen::flashMessagesControl() {
  drawMessages(true);
  delay(90);
  drawMessages(false);
}

void HomeScreen::flashAlarmControl() {
  drawAlarm(true);
  delay(90);
  drawAlarm(false);
}
