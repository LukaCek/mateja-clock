#include "MessageScreens.h"

#include <cstring>

namespace {

// Palette — warm, romantic, glassmorphism-inspired.
constexpr uint16_t kBackground = 0x1082;
constexpr uint16_t kPanel = 0x2104;
constexpr uint16_t kPanelRaised = 0x39A7;
constexpr uint16_t kGlassFill = 0x2945;
constexpr uint16_t kGlassRead = 0x2104;
constexpr uint16_t kGlassBorder = 0x528A;
constexpr uint16_t kWarmWhite = 0xFF7B;
constexpr uint16_t kMuted = 0xAD55;
constexpr uint16_t kCoral = 0xEBAC;
constexpr uint16_t kCoralSoft = 0xD2EA;
constexpr uint16_t kCoralDark = 0xA9E8;

constexpr std::size_t kMaxLineBuffer = 512;

// ── Glass panel helpers ───────────────────────────────────────────────

void drawGlassPanel(Adafruit_GFX& d, int16_t x, int16_t y, int16_t w,
                    int16_t h, uint8_t r) {
  d.fillRoundRect(x, y, w, h, r, kGlassFill);
  d.drawRoundRect(x, y, w, h, r, kGlassBorder);
}

// ── Text wrapping ─────────────────────────────────────────────────────

std::size_t wrapText(U8G2_FOR_ADAFRUIT_GFX& text, const char* input,
                     char* output, std::size_t outputSize, int16_t maxWidth) {
  if (input == nullptr || output == nullptr || outputSize == 0) {
    return 0;
  }
  output[0] = '\0';
  std::size_t outLen = 0;
  std::size_t lines = 0;
  char line[kMaxLineBuffer];
  std::size_t lineLen = 0;
  const char* cursor = input;

  for (;;) {
    const char* wordStart = cursor;
    std::size_t wordLen = 0;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\n') {
      ++cursor;
      ++wordLen;
    }
    if (lineLen > 0 && wordLen > 0) {
      std::size_t cand = lineLen + 1 + wordLen;
      char measure[kMaxLineBuffer];
      if (cand + 1 <= sizeof(measure)) {
        std::memcpy(measure, line, lineLen);
        measure[lineLen] = ' ';
        std::memcpy(measure + lineLen + 1, wordStart, wordLen);
        measure[cand] = '\0';
        if (text.getUTF8Width(measure) > maxWidth) {
          if (outLen + lineLen + 1 < outputSize) {
            std::memcpy(output + outLen, line, lineLen);
            outLen += lineLen;
            output[outLen++] = '\n';
            ++lines;
          }
          lineLen = 0;
        }
      }
    }
    if (wordLen > 0) {
      if (lineLen > 0) {
        line[lineLen++] = ' ';
      }
      std::size_t copy = wordLen < sizeof(line) - lineLen - 1
                              ? wordLen : sizeof(line) - lineLen - 1;
      std::memcpy(line + lineLen, wordStart, copy);
      lineLen += copy;
    }
    if (*cursor == '\0') break;
    if (*cursor == '\n') {
      if (outLen + lineLen + 1 < outputSize) {
        std::memcpy(output + outLen, line, lineLen);
        outLen += lineLen;
        output[outLen++] = '\n';
        ++lines;
      }
      lineLen = 0;
      ++cursor;
      continue;
    }
    ++cursor;
  }
  if (lineLen > 0) {
    if (outLen + lineLen + 1 < outputSize) {
      std::memcpy(output + outLen, line, lineLen);
      outLen += lineLen;
      output[outLen++] = '\n';
      ++lines;
    }
  }
  output[outLen] = '\0';
  return lines;
}

void drawBackChevron(Adafruit_GFX& d, int16_t x, int16_t y) {
  d.drawLine(x + 8, y, x, y + 7, kWarmWhite);
  d.drawLine(x, y + 7, x + 8, y + 14, kWarmWhite);
}

}  // namespace

MessagePopup::MessagePopup(Adafruit_GFX& display,
                           U8G2_FOR_ADAFRUIT_GFX& text,
                           const MessageService& store)
    : display_(display), text_(text), store_(store) {}

void MessagePopup::show(std::size_t messageIndex) {
  messageIndex_ = messageIndex;
  active_ = true;
  shownAtMs_ = millis();
}

void MessagePopup::update() {
  if (active_ && millis() - shownAtMs_ >= kPopupDurationMs) {
    active_ = false;
  }
}

void MessagePopup::dismiss() { active_ = false; }

void MessagePopup::draw() {
  if (!active_) return;
  if (messageIndex_ >= store_.count()) { active_ = false; return; }

  const MessageService::Summary& msg = store_.at(messageIndex_);
  const int16_t x = 22;
  const int16_t y = kPopupY;
  const int16_t w = display_.width() - 44;
  const int16_t h = kPopupHeight;

  display_.fillRoundRect(x + 3, y + 3, w, h, 12, 0x0841);
  display_.fillRoundRect(x, y, w, h, 12, kGlassFill);
  display_.drawRoundRect(x, y, w, h, 12, kCoralSoft);
  display_.fillRoundRect(x, y, 5, h, 3, kCoral);

  text_.setFontMode(1);
  text_.setFont(u8g2_font_5x8_tf);
  text_.setForegroundColor(kCoral);
  text_.drawUTF8(x + 14, y + 13, u8"NOVO SPOROČILO");

  text_.setFont(u8g2_font_helvB12_te);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(x + 14, y + 32, msg.sender);

  char preview[48];
  messagelogic::copyPreviewUtf8(msg.preview, preview, sizeof(preview), 42);
  text_.setFont(u8g2_font_6x12_te);
  text_.setForegroundColor(kMuted);
  text_.drawUTF8(x + 14, y + 50, preview);

  text_.setFont(u8g2_font_5x8_tf);
  text_.setForegroundColor(kCoralSoft);
  const char* hint = u8"dotakni se za ogled";
  text_.drawUTF8(x + w - 12 - text_.getUTF8Width(hint), y + 63, hint);
}

// ── Messages list ─────────────────────────────────────────────────────

MessagesListScreen::MessagesListScreen(Adafruit_GFX& display,
                                       U8G2_FOR_ADAFRUIT_GFX& text,
                                       const MessageService& store,
                                       const TimeService& time)
    : display_(display), text_(text), store_(store), time_(time) {}

void MessagesListScreen::draw() {
  display_.fillScreen(kBackground);

  // Premium header with back chevron and count
  display_.fillRoundRect(4, 4, display_.width() - 8, 34, 11, kPanelRaised);
  display_.drawRoundRect(4, 4, display_.width() - 8, 34, 11, kGlassBorder);
  drawBackChevron(display_, 14, 13);

  text_.setFont(u8g2_font_helvB14_te);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(36, 18, u8"Sporočila");

  // Subtitle sender name
  text_.setFont(u8g2_font_6x12_tf);
  text_.setForegroundColor(kCoralSoft);
  text_.drawUTF8(36, 32, "Luka");

  if (store_.count() == 0) {
    text_.setFont(u8g2_font_9x15_te);
    text_.setForegroundColor(kMuted);
    const char* empty = u8"Ni še sporočil";
    text_.drawUTF8((display_.width() - text_.getUTF8Width(empty)) / 2, 120,
                   empty);
    return;
  }

  const std::size_t total = store_.count();
  const std::size_t maxOffset = total > kVisibleRows ? total - kVisibleRows : 0;
  if (scrollOffset_ > maxOffset) scrollOffset_ = maxOffset;

  const int16_t cardH = 40;
  const int16_t gap = 5;
  const int16_t listTop = 44;

  for (std::size_t row = 0; row < kVisibleRows; ++row) {
    const std::size_t idx = scrollOffset_ + row;
    if (idx >= total) break;

    const MessageService::Summary& msg = store_.at(idx);
    const int16_t cy = listTop + static_cast<int16_t>(row * (cardH + gap));
    const int16_t cx = 6;
    const int16_t cw = display_.width() - 12;

    // Glass card
    drawGlassPanel(display_, cx, cy, cw, cardH, 10);

    // Unread accent
    if (!msg.read) {
      display_.fillRoundRect(cx + 3, cy + 6, 4, cardH - 12, 2, kCoral);
    }

    // Time chip right-aligned
    char timeLabel[32];
    if (messagelogic::formatMessageTimeLabel(
            msg.time, time_.snapshot().epochSeconds,
            time_.utcOffsetSeconds(), time_.snapshot().valid, timeLabel,
            sizeof(timeLabel))) {
      text_.setFont(u8g2_font_5x8_tf);
      text_.setForegroundColor(msg.read ? kMuted : kCoralSoft);
      text_.drawUTF8(cx + cw - 6 - text_.getUTF8Width(timeLabel), cy + 8,
                     timeLabel);
    }

    // Preview (main message text, bigger font)
    text_.setFont(u8g2_font_9x15_tf);
    text_.setForegroundColor(msg.read ? kMuted : 0xD6B8);
    text_.drawUTF8(cx + 12, cy + 28, msg.preview);
  }

  // ── Scroll bar row ─────────────────────────────────────────────
  const int16_t scrollBarY = listTop + kVisibleRows * (cardH + gap);
  const int16_t scrollBarH = display_.height() - scrollBarY;

  display_.fillRoundRect(4, scrollBarY, display_.width() - 8, scrollBarH,
                         10, kPanelRaised);
  display_.drawRoundRect(4, scrollBarY, display_.width() - 8, scrollBarH,
                         10, kGlassBorder);

  // Up button (left half) - big touch target
  text_.setFont(u8g2_font_logisoso24_tf);
  text_.setForegroundColor(scrollOffset_ > 0 ? kWarmWhite : kMuted);
  text_.drawUTF8(30, scrollBarY + 30, u8"\u25B2");

  // Page info centered
  text_.setFont(u8g2_font_9x15_tf);
  text_.setForegroundColor(kCoralSoft);
  char pageInfo[16];
  std::snprintf(pageInfo, sizeof(pageInfo), "%u/%u",
                static_cast<unsigned>(scrollOffset_ / kVisibleRows + 1),
                static_cast<unsigned>((total + kVisibleRows - 1) /
                                      kVisibleRows));
  text_.drawUTF8((display_.width() - text_.getUTF8Width(pageInfo)) / 2,
                 scrollBarY + 22, pageInfo);

  // Down button (right half) - big touch target
  text_.setFont(u8g2_font_logisoso24_tf);
  text_.setForegroundColor(scrollOffset_ < maxOffset ? kWarmWhite : kMuted);
  text_.drawUTF8(display_.width() - 55, scrollBarY + 30, u8"\u25BC");
}

MessagesListScreen::Action MessagesListScreen::handleTap(int16_t x, int16_t y) {
  if (y < kHeaderHeight + 4) return Action::Back;
  if (store_.count() == 0) return Action::None;

  const int16_t cardH = 40;
  const int16_t gap = 5;
  const int16_t listTop = 44;
  const int16_t scrollBarY = listTop + kVisibleRows * (cardH + gap);

  // Scroll bar row
  if (y >= scrollBarY) {
    const std::size_t total = store_.count();
    const std::size_t maxOffset = total > kVisibleRows ? total - kVisibleRows : 0;
    if (x < display_.width() / 2 && scrollOffset_ >= kVisibleRows) {
      scrollOffset_ -= kVisibleRows;
      return Action::None;
    }
    if (x >= display_.width() / 2 && scrollOffset_ < maxOffset) {
      scrollOffset_ += kVisibleRows;
      if (scrollOffset_ > maxOffset) scrollOffset_ = maxOffset;
      return Action::None;
    }
    return Action::None;
  }

  const std::size_t row =
      static_cast<std::size_t>((y - listTop) / (cardH + gap));
  if (row >= kVisibleRows) return Action::None;

  const std::size_t total = store_.count();
  const std::size_t index = scrollOffset_ + row;
  if (index >= total) return Action::None;
  selectedIndex_ = index;
  return Action::OpenDetail;
}

// ── Message detail ────────────────────────────────────────────────────

MessageDetailScreen::MessageDetailScreen(Adafruit_GFX& display,
                                         U8G2_FOR_ADAFRUIT_GFX& text,
                                         MessageService& store,
                                         const TimeService& time)
    : display_(display), text_(text), store_(store), time_(time) {}

void MessageDetailScreen::open(std::size_t index) {
  messageIndex_ = index;
  opened_ = true;
  store_.markReadAtIndex(index);
}

void MessageDetailScreen::draw() {
  if (!opened_ || messageIndex_ >= store_.count()) return;

  const MessageService::Summary& msg = store_.at(messageIndex_);
  char sender[64];
  char body[messagelogic::kMaxTextBytes];
  bool detail = store_.openDetail(messageIndex_, sender, sizeof(sender),
                                  body, sizeof(body));

  display_.fillScreen(kBackground);

  // Premium header
  display_.fillRoundRect(4, 4, display_.width() - 8, 34, 11, kPanelRaised);
  display_.drawRoundRect(4, 4, display_.width() - 8, 34, 11, kGlassBorder);

  // Big back button area (touch-friendly)
  text_.setFont(u8g2_font_helvB18_te);
  text_.setFontMode(1);
  text_.setForegroundColor(kCoral);
  text_.drawUTF8(14, 27, u8"\u2190");

  text_.setFontMode(1);
  text_.setFont(u8g2_font_helvB12_te);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(36, 27, detail ? sender : msg.sender);

  char timeLabel[32];
  if (messagelogic::formatMessageTimeLabel(
          msg.time, time_.snapshot().epochSeconds,
          time_.utcOffsetSeconds(), time_.snapshot().valid, timeLabel,
          sizeof(timeLabel))) {
    text_.setFont(u8g2_font_5x8_tf);
    text_.setForegroundColor(kCoralSoft);
    text_.drawUTF8(display_.width() - 12 - text_.getUTF8Width(timeLabel), 24,
                   timeLabel);
  }

  // Body in a glass card
  const int16_t bodyTop = 48;
  const int16_t bodyH = display_.height() - bodyTop - kFooterHeight - 2;
  const int16_t bodyX = 4;
  const int16_t bodyW = display_.width() - 8;
  drawGlassPanel(display_, bodyX, bodyTop, bodyW, bodyH, 8);

  text_.setFont(u8g2_font_9x15_tf);
  text_.setForegroundColor(kWarmWhite);
  const int16_t bodyMaxWidth = bodyW - 16;
  char wrapped[kMaxLineBuffer];
  const std::size_t lines = wrapText(
      text_, detail ? body : msg.preview, wrapped, sizeof(wrapped),
      bodyMaxWidth);

  int16_t y = bodyTop + 10;
  const char* line = wrapped;
  for (std::size_t i = 0; i < lines; ++i) {
    const char* nl = std::strchr(line, '\n');
    const std::size_t len =
        nl != nullptr ? static_cast<std::size_t>(nl - line) : std::strlen(line);
    char buf[512];
    const std::size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    std::memcpy(buf, line, n);
    buf[n] = '\0';
    text_.drawUTF8(bodyX + 8, y, buf);
    y += 17;
    if (y > bodyTop + bodyH - 4) break;
    line = (nl != nullptr) ? nl + 1 : nullptr;
    if (line == nullptr) break;
  }

  // Footer button
  const int16_t btnY = display_.height() - kFooterHeight;
  display_.fillRoundRect(8, btnY, display_.width() - 16, 30, 10, kPanelRaised);
  display_.drawRoundRect(8, btnY, display_.width() - 16, 30, 10, kCoralDark);
  text_.setFont(u8g2_font_helvB18_te);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8((display_.width() - text_.getUTF8Width("Zapri")) / 2,
                 btnY + 22, "Zapri");
}

MessageDetailScreen::Action MessageDetailScreen::handleTap(int16_t x,
                                                           int16_t y) {
  if (y >= display_.height() - kFooterHeight || y < kHeaderHeight) {
    opened_ = false;
    return Action::Back;
  }
  return Action::None;
}
