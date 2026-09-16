#include "AlarmScreens.h"

#include <cstdio>

namespace {

constexpr uint16_t kBackground = 0x1082;
constexpr uint16_t kPanel = 0x2104;
constexpr uint16_t kPanelRaised = 0x3186;
constexpr uint16_t kWarmWhite = 0xFF7B;
constexpr uint16_t kMuted = 0xAD55;
constexpr uint16_t kCoral = 0xEBAC;
constexpr uint16_t kCoralDark = 0xA9E8;
constexpr uint16_t kError = 0xF986;

// Draws a bold, high-contrast X centered over the given rect. Used to mark a
// control as physically disabled (hardware gate closed).
void drawDisabledX(Adafruit_GFX& display, const AlarmScreenRect& rect) {
  constexpr int16_t kThickness = 3;
  const int16_t left = rect.x + 9;
  const int16_t right = rect.x + rect.width - 9;
  const int16_t top = rect.y + 9;
  const int16_t bottom = rect.y + rect.height - 9;
  for (int16_t offset = 0; offset < kThickness; ++offset) {
    display.drawLine(left, top + offset, right, bottom + offset, kBackground);
    display.drawLine(left, bottom - offset, right, top - offset, kBackground);
  }
}

void setFont(U8G2_FOR_ADAFRUIT_GFX& text, const uint8_t* font,
             uint16_t color) {
  text.setFont(font);
  text.setFontMode(1);
  text.setForegroundColor(color);
}

void centeredText(U8G2_FOR_ADAFRUIT_GFX& text, const char* value,
                  int16_t centerX, int16_t baseline) {
  text.drawUTF8(centerX - text.getUTF8Width(value) / 2, baseline, value);
}

void drawButton(Adafruit_GFX& display, const AlarmScreenRect& rect,
                uint16_t fill, uint16_t border) {
  display.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, fill);
  display.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 8, border);
}

const char* daysSummary(uint8_t mask) {
  if (mask == 0x1F) return u8"Vsak delavnik";
  if (mask == 0x7F) return u8"Vsak dan";
  if (mask == 0x60) return u8"Vikend";
  return u8"Izbrani dnevi";
}

}

bool AlarmScreenRect::contains(int16_t pointX, int16_t pointY) const {
  return pointX >= x && pointY >= y && pointX < x + width &&
         pointY < y + height;
}

AlarmSettingsScreen::AlarmSettingsScreen(
    Adafruit_GFX& display, U8G2_FOR_ADAFRUIT_GFX& text)
    : display_(display),
      text_(text),
      edit_(),
      saveError_(false),
      hardwareAllowed_(true) {}

void AlarmSettingsScreen::beginEdit(const alarmclock::AlarmConfig& config) {
  edit_ = config;
  saveError_ = false;
}

const alarmclock::AlarmConfig& AlarmSettingsScreen::editConfig() const {
  return edit_;
}

alarmclock::AlarmConfig& AlarmSettingsScreen::editConfig() { return edit_; }

void AlarmSettingsScreen::setSaveError(bool error) { saveError_ = error; }

void AlarmSettingsScreen::setHardwareAllowed(bool allowed) {
  if (hardwareAllowed_ == allowed) return;
  hardwareAllowed_ = allowed;
  draw();
}

AlarmScreenRect AlarmSettingsScreen::backTarget() { return {0, 0, 48, 44}; }
AlarmScreenRect AlarmSettingsScreen::enabledTarget() {
  return {244, 8, 64, 32};
}
AlarmScreenRect AlarmSettingsScreen::hourMinusTarget() {
  return {18, 73, 44, 54};
}
AlarmScreenRect AlarmSettingsScreen::hourPlusTarget() {
  return {112, 73, 44, 54};
}
AlarmScreenRect AlarmSettingsScreen::minuteMinusTarget() {
  return {164, 73, 44, 54};
}
AlarmScreenRect AlarmSettingsScreen::minutePlusTarget() {
  return {258, 73, 44, 54};
}
AlarmScreenRect AlarmSettingsScreen::dayTarget(uint8_t day) {
  return {static_cast<int16_t>(9 + day * 44), 142, 38, 32};
}
AlarmScreenRect AlarmSettingsScreen::saveTarget() { return {80, 198, 160, 36}; }

void AlarmSettingsScreen::draw() {
  display_.fillScreen(kBackground);
  display_.fillRect(0, 0, 320, 48, kPanel);

  display_.drawLine(30, 13, 18, 22, kWarmWhite);
  display_.drawLine(18, 22, 30, 31, kWarmWhite);
  display_.drawLine(18, 22, 38, 22, kWarmWhite);
  setFont(text_, u8g2_font_helvB14_te, kWarmWhite);
  text_.drawUTF8(54, 30, u8"Budilka");

  const AlarmScreenRect toggle = enabledTarget();
  display_.fillRoundRect(toggle.x, toggle.y, toggle.width, toggle.height, 16,
                         edit_.softwareEnabled ? kCoral : kPanelRaised);
  display_.fillCircle(edit_.softwareEnabled ? toggle.x + 48 : toggle.x + 16,
                      toggle.y + 16, 12, kWarmWhite);
  if (!hardwareAllowed_) {
    drawDisabledX(display_, toggle);
  }

  setFont(text_, u8g2_font_6x12_te, kMuted);
  text_.drawUTF8(18, 65, u8"URA");
  text_.drawUTF8(164, 65, u8"MINUTA");

  const AlarmScreenRect controls[] = {
      hourMinusTarget(), hourPlusTarget(), minuteMinusTarget(),
      minutePlusTarget()};
  for (uint8_t i = 0; i < 4; ++i) {
    drawButton(display_, controls[i], kPanel, kPanelRaised);
  }
  setFont(text_, u8g2_font_helvB18_te, kWarmWhite);
  centeredText(text_, u8"−", 40, 108);
  centeredText(text_, u8"+", 134, 108);
  centeredText(text_, u8"−", 186, 108);
  centeredText(text_, u8"+", 280, 108);

  char value[3];
  setFont(text_, u8g2_font_logisoso38_tf, kWarmWhite);
  std::snprintf(value, sizeof(value), "%02u", static_cast<unsigned>(edit_.hour));
  centeredText(text_, value, 87, 117);
  std::snprintf(value, sizeof(value), "%02u",
                static_cast<unsigned>(edit_.minute));
  centeredText(text_, value, 233, 117);
  setFont(text_, u8g2_font_helvB18_te, kMuted);
  centeredText(text_, ":", 160, 111);

  const char* labels[] = {"P", "T", "S", u8"Č", "P", "S", "N"};
  for (uint8_t day = 0; day < 7; ++day) {
    const AlarmScreenRect target = dayTarget(day);
    const bool selected = (edit_.daysMask & (1U << day)) != 0;
    drawButton(display_, target, selected ? kCoral : kPanel,
               selected ? kCoral : kPanelRaised);
    setFont(text_, u8g2_font_helvB12_te,
            selected ? kWarmWhite : kMuted);
    centeredText(text_, labels[day], target.x + target.width / 2, 165);
  }

  setFont(text_, u8g2_font_6x12_te, saveError_ ? kError : kMuted);
  const char* summary = saveError_ ? u8"Shranjevanje ni uspelo"
                                   : daysSummary(edit_.daysMask);
  centeredText(text_, summary, 160, 191);

  const AlarmScreenRect save = saveTarget();
  drawButton(display_, save, kCoral, kCoralDark);
  setFont(text_, u8g2_font_helvB14_te, kWarmWhite);
  centeredText(text_, u8"Shrani", 160, 224);
}

AlarmSettingsScreen::Action AlarmSettingsScreen::handleTap(int16_t x,
                                                            int16_t y) {
  saveError_ = false;
  if (backTarget().contains(x, y)) return Action::Cancel;
  if (enabledTarget().contains(x, y)) {
    edit_.softwareEnabled = !edit_.softwareEnabled;
    return Action::None;
  }
  if (hourMinusTarget().contains(x, y)) {
    edit_.hour = edit_.hour == 0 ? 23 : edit_.hour - 1;
  } else if (hourPlusTarget().contains(x, y)) {
    edit_.hour = edit_.hour == 23 ? 0 : edit_.hour + 1;
  } else if (minuteMinusTarget().contains(x, y)) {
    edit_.minute = edit_.minute == 0 ? 59 : edit_.minute - 1;
  } else if (minutePlusTarget().contains(x, y)) {
    edit_.minute = edit_.minute == 59 ? 0 : edit_.minute + 1;
  } else {
    for (uint8_t day = 0; day < 7; ++day) {
      if (dayTarget(day).contains(x, y)) {
        const uint8_t bit = static_cast<uint8_t>(1U << day);
        if ((edit_.daysMask & bit) != 0 && edit_.daysMask == bit) {
          saveError_ = true;
        } else {
          edit_.daysMask ^= bit;
        }
        return Action::None;
      }
    }
    if (saveTarget().contains(x, y)) return Action::Save;
  }
  return Action::None;
}

RingingScreen::RingingScreen(Adafruit_GFX& display,
                             U8G2_FOR_ADAFRUIT_GFX& text)
    : display_(display), text_(text) {}

AlarmScreenRect RingingScreen::snoozeTarget() { return {4, 148, 154, 88}; }
AlarmScreenRect RingingScreen::stopTarget() { return {162, 148, 154, 88}; }

void RingingScreen::draw(uint8_t hour, uint8_t minute,
                         uint8_t snoozeMinutes, bool photoAvailable) {
  display_.fillScreen(photoAvailable ? 0x0841 : kBackground);
  display_.fillRect(0, 0, 320, 240, 0x0841);
  display_.fillRoundRect(30, 16, 260, 34, 17, kPanel);
  setFont(text_, u8g2_font_helvB14_te, kWarmWhite);
  centeredText(text_, u8"Dobro jutro ♥", 160, 40);

  char time[6];
  std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(hour),
                static_cast<unsigned>(minute));
  setFont(text_, u8g2_font_logisoso50_tf, kWarmWhite);
  centeredText(text_, time, 160, 126);

  const AlarmScreenRect snooze = snoozeTarget();
  const AlarmScreenRect stop = stopTarget();
  drawButton(display_, snooze, kCoral, kCoralDark);
  drawButton(display_, stop, kPanelRaised, kMuted);

  char snoozeLabel[24];
  std::snprintf(snoozeLabel, sizeof(snoozeLabel), u8"Dremež +%u min",
                static_cast<unsigned>(snoozeMinutes));
  setFont(text_, u8g2_font_helvB12_te, kWarmWhite);
  centeredText(text_, snoozeLabel, snooze.x + snooze.width / 2, 202);
  centeredText(text_, u8"Ugasni", stop.x + stop.width / 2, 202);
}

RingingScreen::Action RingingScreen::handleTap(int16_t x, int16_t y) const {
  if (snoozeTarget().contains(x, y)) return Action::Snooze;
  if (stopTarget().contains(x, y)) return Action::Stop;
  return Action::None;
}
