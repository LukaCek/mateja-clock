#include "MessageScreens.h"

#include <cstring>

namespace {

// Palette (shared with the other screens).
constexpr uint16_t kBackground = 0x1082;
constexpr uint16_t kPanel = 0x2104;
constexpr uint16_t kPanelRaised = 0x3186;
constexpr uint16_t kWarmWhite = 0xFF7B;
constexpr uint16_t kMuted = 0xC5F2;
constexpr uint16_t kCoral = 0xEBAC;
constexpr uint16_t kCoralDark = 0xA9E8;

// Maximum bytes for the wrapped-line scratch buffer.
constexpr std::size_t kMaxLineBuffer = 512;

// Splits `input` into lines that each fit `maxWidth` at the current font,
// preferring breaks at spaces, keeping whole words together. Emits newline
// separated lines into `output`. Returns the number of lines.
std::size_t wrapText(U8G2_FOR_ADAFRUIT_GFX& text, const char* input,
                     char* output, std::size_t outputSize, int16_t maxWidth) {
  if (input == nullptr || output == nullptr || outputSize == 0) {
    return 0;
  }
  output[0] = '\0';
  std::size_t outLength = 0;
  std::size_t lines = 0;

  char line[kMaxLineBuffer];
  std::size_t lineLength = 0;

  const char* cursor = input;
  for (;;) {
    const char* wordStart = cursor;
    std::size_t wordLength = 0;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\n') {
      ++cursor;
      ++wordLength;
    }

    // Will the current line plus this word still fit?
    if (lineLength > 0 && wordLength > 0) {
      const std::size_t candidateLength = lineLength + 1 + wordLength;
      char measure[kMaxLineBuffer];
      if (candidateLength + 1 <= sizeof(measure)) {
        std::memcpy(measure, line, lineLength);
        measure[lineLength] = ' ';
        std::memcpy(measure + lineLength + 1, wordStart, wordLength);
        measure[candidateLength] = '\0';
        if (text.getUTF8Width(measure) > maxWidth) {
          // Emit the current line and start a fresh one with this word.
          if (outLength + lineLength + 1 < outputSize) {
            std::memcpy(output + outLength, line, lineLength);
            outLength += lineLength;
            output[outLength++] = '\n';
            ++lines;
          }
          lineLength = 0;
        }
      }
    }

    if (wordLength > 0) {
      if (lineLength > 0) {
        line[lineLength++] = ' ';
      }
      const std::size_t copyLength =
          wordLength < sizeof(line) - lineLength - 1
              ? wordLength
              : sizeof(line) - lineLength - 1;
      std::memcpy(line + lineLength, wordStart, copyLength);
      lineLength += copyLength;
    }

    if (*cursor == '\0') {
      break;
    }
    if (*cursor == '\n') {
      if (outLength + lineLength + 1 < outputSize) {
        std::memcpy(output + outLength, line, lineLength);
        outLength += lineLength;
        output[outLength++] = '\n';
        ++lines;
      }
      lineLength = 0;
      ++cursor;
      continue;
    }
    ++cursor;  // skip the space
  }

  if (lineLength > 0) {
    if (outLength + lineLength + 1 < outputSize) {
      std::memcpy(output + outLength, line, lineLength);
      outLength += lineLength;
      output[outLength++] = '\n';
      ++lines;
    }
  }
  output[outLength] = '\0';
  return lines;
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
  if (!active_) {
    return;
  }
  if (messageIndex_ >= store_.count()) {
    active_ = false;
    return;
  }
  const MessageService::Summary& message = store_.at(messageIndex_);

  display_.fillRoundRect(2, kPopupY, display_.width() - 4, kPopupHeight, 8,
                         kPanelRaised);
  display_.drawRoundRect(2, kPopupY, display_.width() - 4, kPopupHeight, 8,
                         kCoralDark);
  display_.fillCircle(16, kPopupY + 12, 4, kCoral);

  text_.setFontMode(1);
  text_.setFont(u8g2_font_9x15_te);
  text_.setForegroundColor(kWarmWhite);
  char sender[72];
  std::snprintf(sender, sizeof(sender), "Sporočilo od %s",
                message.sender);
  text_.drawUTF8(28, kPopupY + 15, sender);

  text_.setFont(u8g2_font_6x12_te);
  text_.setForegroundColor(kMuted);
  text_.drawUTF8(28, kPopupY + 33, message.preview);
}

MessagesListScreen::MessagesListScreen(Adafruit_GFX& display,
                                       U8G2_FOR_ADAFRUIT_GFX& text,
                                       const MessageService& store,
                                       const TimeService& time)
    : display_(display), text_(text), store_(store), time_(time) {}

void MessagesListScreen::draw() {
  display_.fillScreen(kBackground);

  display_.fillRect(0, 0, display_.width(), kHeaderHeight, kPanel);
  text_.setFont(u8g2_font_9x15_te);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(8, 14, "Sporočila");

  const char* close = "Zapri";
  text_.setFont(u8g2_font_6x12_te);
  text_.setForegroundColor(kCoralDark);
  text_.drawUTF8(display_.width() - 8 - text_.getUTF8Width(close), 13, close);

  if (store_.count() == 0) {
    text_.setFont(u8g2_font_9x15_te);
    text_.setForegroundColor(kMuted);
    const char* empty = "Ni še sporočil";
    text_.drawUTF8((display_.width() - text_.getUTF8Width(empty)) / 2, 120,
                   empty);
    return;
  }

  const std::size_t total = store_.count();
  const std::size_t maxOffset = total > kVisibleRows ? total - kVisibleRows : 0;
  if (scrollOffset_ > maxOffset) {
    scrollOffset_ = maxOffset;
  }

  for (std::size_t row = 0; row < kVisibleRows; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= total) {
      break;
    }
    const MessageService::Summary& message = store_.at(index);
    const int16_t y = kHeaderHeight + static_cast<int16_t>(row * kRowHeight);

    if (!message.read) {
      display_.fillRect(0, y, 4, kRowHeight - 2, kCoral);
    }

    text_.setFont(u8g2_font_9x15_te);
    text_.setForegroundColor(message.read ? kMuted : kWarmWhite);
    text_.drawUTF8(12, y + 13, message.sender);

    text_.setFont(u8g2_font_6x12_te);
    text_.setForegroundColor(message.read ? kMuted : kWarmWhite);
    text_.drawUTF8(12, y + kRowHeight - 5, message.preview);

    char timeLabel[32];
    if (messagelogic::formatMessageTimeLabel(
            message.time, time_.snapshot().epochSeconds,
            time_.utcOffsetSeconds(), time_.snapshot().valid, timeLabel,
            sizeof(timeLabel))) {
      text_.setFont(u8g2_font_6x12_te);
      text_.setForegroundColor(message.read ? kMuted : kCoral);
      text_.drawUTF8(display_.width() - 8 - text_.getUTF8Width(timeLabel),
                     y + 13, timeLabel);
    }

    if (row + 1 < kVisibleRows) {
      display_.drawFastHLine(8, y + kRowHeight - 1, display_.width() - 16,
                             kPanel);
    }
  }

  // Scroll footer.
  text_.setFont(u8g2_font_5x8_tf);
  text_.setForegroundColor(kMuted);
  if (scrollOffset_ > 0 || scrollOffset_ < maxOffset) {
    const char* hint =
        scrollOffset_ > 0 ? "^ novejše" : "starejše >";
    text_.drawUTF8(8, display_.height() - 6, hint);
  }
}

MessagesListScreen::Action MessagesListScreen::handleTap(int16_t x,
                                                         int16_t y) {
  if (y < kHeaderHeight) {
    return Action::Back;
  }
  if (store_.count() == 0) {
    return Action::None;
  }
  const std::size_t row =
      static_cast<std::size_t>(y - kHeaderHeight) / kRowHeight;
  if (row >= kVisibleRows) {
    return Action::None;
  }
  const std::size_t total = store_.count();
  const std::size_t maxOffset = total > kVisibleRows ? total - kVisibleRows : 0;
  if (row == kVisibleRows - 1 &&
      y >= kHeaderHeight + static_cast<int16_t>(kVisibleRows * kRowHeight) -
               kRowHeight / 2) {
    // Bottom half of the last row: previous page.
    if (x < display_.width() / 2 && scrollOffset_ >= kVisibleRows) {
      scrollOffset_ -= kVisibleRows;
      return Action::None;
    }
    if (x >= display_.width() / 2 && scrollOffset_ < maxOffset) {
      scrollOffset_ += kVisibleRows;
      if (scrollOffset_ > maxOffset) {
        scrollOffset_ = maxOffset;
      }
      return Action::None;
    }
  }
  const std::size_t index = scrollOffset_ + row;
  if (index >= total) {
    return Action::None;
  }
  selectedIndex_ = index;
  return Action::OpenDetail;
}

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
  if (!opened_ || messageIndex_ >= store_.count()) {
    return;
  }
  const MessageService::Summary& message = store_.at(messageIndex_);
  char sender[64];
  char textBuffer[messagelogic::kMaxTextBytes];
  bool detail = store_.openDetail(messageIndex_, sender, sizeof(sender),
                                  textBuffer, sizeof(textBuffer));

  display_.fillScreen(kBackground);
  display_.fillRect(0, 0, display_.width(), kHeaderHeight, kPanel);
  text_.setFont(u8g2_font_9x15_te);
  text_.setFontMode(1);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8(8, 14, detail ? sender : message.sender);

  const char* close = "Zapri";
  text_.setFont(u8g2_font_6x12_te);
  text_.setForegroundColor(kCoralDark);
  text_.drawUTF8(display_.width() - 8 - text_.getUTF8Width(close), 13, close);

  char timeLabel[32];
  if (messagelogic::formatMessageTimeLabel(
          message.time, time_.snapshot().epochSeconds,
          time_.utcOffsetSeconds(), time_.snapshot().valid, timeLabel,
          sizeof(timeLabel))) {
    text_.setFont(u8g2_font_6x12_te);
    text_.setForegroundColor(kMuted);
    text_.drawUTF8(8, 30, timeLabel);
  }

  display_.drawFastHLine(8, 34, display_.width() - 16, kPanel);

  text_.setFont(u8g2_font_9x15_te);
  text_.setForegroundColor(kWarmWhite);
  const int16_t bodyMaxWidth = display_.width() - 24;
  char wrapped[kMaxLineBuffer];
  const std::size_t lines = wrapText(
      text_, detail ? textBuffer : message.preview, wrapped, sizeof(wrapped),
      bodyMaxWidth);
  int16_t y = 44;
  const char* line = wrapped;
  for (std::size_t i = 0; i < lines; ++i) {
    const char* newline = std::strchr(line, '\n');
    const std::size_t length =
        newline != nullptr ? static_cast<std::size_t>(newline - line)
                           : std::strlen(line);
    char lineBuffer[512];
    const std::size_t copyLength =
        length < sizeof(lineBuffer) - 1 ? length : sizeof(lineBuffer) - 1;
    std::memcpy(lineBuffer, line, copyLength);
    lineBuffer[copyLength] = '\0';
    text_.drawUTF8(12, y, lineBuffer);
    y += 17;
    if (y > display_.height() - kFooterHeight - 4) {
      break;
    }
    if (newline != nullptr) {
      line = newline + 1;
    } else {
      break;
    }
  }

  const int16_t buttonY = display_.height() - kFooterHeight;
  display_.fillRoundRect(8, buttonY, display_.width() - 16, 24, 6,
                         kPanelRaised);
  display_.drawRoundRect(8, buttonY, display_.width() - 16, 24, 6, kCoralDark);
  text_.setFont(u8g2_font_9x15_te);
  text_.setForegroundColor(kWarmWhite);
  text_.drawUTF8((display_.width() - text_.getUTF8Width("Zapri")) / 2,
                 buttonY + 17, "Zapri");
}

MessageDetailScreen::Action MessageDetailScreen::handleTap(int16_t x,
                                                           int16_t y) {
  const int16_t buttonY = display_.height() - kFooterHeight;
  if (y >= buttonY) {
    opened_ = false;
    return Action::Back;
  }
  if (y < kHeaderHeight) {
    opened_ = false;
    return Action::Back;
  }
  return Action::None;
}