#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "MessageService.h"
#include "TimeService.h"

// Popup overlay shown on the Home screen whenever a new unread message
// arrives. Tapping the popup opens the full detail view; the popup
// auto-dismisses after a few seconds.
class MessagePopup {
 public:
  MessagePopup(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text,
               const MessageService& store);

  // Shows the popup for the message at `messageIndex`.
  void show(std::size_t messageIndex);

  // Draws the popup if active. Must be called *after* HomeScreen::draw().
  void draw();

  // Auto-dismisses when the timer expires; call each main-loop tick.
  void update();

  bool active() const { return active_; }
  void dismiss();
  std::size_t messageIndex() const { return messageIndex_; }

 private:
  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  const MessageService& store_;
  bool active_ = false;
  std::size_t messageIndex_ = 0;
  uint32_t shownAtMs_ = 0;
  static constexpr uint16_t kPopupDurationMs = 4500;
  static constexpr int16_t kPopupHeight = 44;
  static constexpr int16_t kPopupY = 196;  // 240 - 44
};

// Scrollable list of recent messages (newest first). Returns an OpenDetail
// action when a row is tapped; Back when the status-bar region is tapped.
class MessagesListScreen {
 public:
  enum class Action { None, Back, OpenDetail };

  MessagesListScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text,
                     const MessageService& store, const TimeService& time);

  void draw();
  Action handleTap(int16_t x, int16_t y);

  // Index of the message the user tapped, valid only when handleTap
  // returns OpenDetail.
  std::size_t selectedIndex() const { return selectedIndex_; }

 private:
  void drawStatusBar();
  void drawRows();
  void drawEmpty();

  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  const MessageService& store_;
  const TimeService& time_;
  std::size_t selectedIndex_ = 0;
  std::size_t scrollOffset_ = 0;
  static constexpr std::size_t kVisibleRows = 7;
  static constexpr int16_t kRowHeight = 28;
  static constexpr int16_t kHeaderHeight = 18;
};

// Full-screen detail view of a single message. Returns Back when the
// "Zapri" button is tapped.
class MessageDetailScreen {
 public:
  enum class Action { None, Back };

  MessageDetailScreen(Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text,
                      MessageService& store, const TimeService& time);

  // Opens the message at `index` and marks it as read (persisting the
  // flag). No-op while another message is already open.
  void open(std::size_t index);
  void draw();
  Action handleTap(int16_t x, int16_t y);

 private:
  Adafruit_GFX& display_;
  U8G2_FOR_ADAFRUIT_GFX& text_;
  MessageService& store_;
  const TimeService& time_;
  std::size_t messageIndex_ = 0;
  bool opened_ = false;
  static constexpr int16_t kHeaderHeight = 18;
  static constexpr int16_t kFooterHeight = 32;
};