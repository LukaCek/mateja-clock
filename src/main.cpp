#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>

#include "AlarmAudio.h"
#include "AlarmLogic.h"
#include "AlarmScreens.h"
#include "AlarmService.h"
#include "BrightnessService.h"
#include "EmojiService.h"
#include "FullReset.h"
#include "HomeScreen.h"
#include "MessageScreens.h"
#include "MessageService.h"
#include "NtfyClient.h"
#include "NtfyConfig.h"
#include "OtaService.h"
#include "PhotoService.h"
#include "RtcLinkService.h"
#include "StatusProviders.h"
#include "TimeService.h"
#include "WavUploader.h"
#include "WifiConfig.h"
#include "hardware_pins.h"
#include "ntfy_ca_cert.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint16_t kFallbackBackground = 0x1082;
constexpr uint16_t kWarmWhite = 0xFF7B;
constexpr uint32_t kMinimumTapMs = 60;
constexpr uint32_t kMaximumTapMs = 800;
constexpr uint32_t kTouchCooldownMs = 250;
constexpr uint32_t kSlideshowIntervalMs = 45000;
// Volume must stay unchanged this long before it is persisted to the SD card.
constexpr uint32_t kVolumeStablePersistMs = 3000;

SPIClass displaySpi(HSPI);
SPIClass sdSpi(VSPI);
Adafruit_ST7789 display(&displaySpi, pins::kTftChipSelect,
                        pins::kTftDataCommand, pins::kTftReset);
U8G2_FOR_ADAFRUIT_GFX unicodeText;
PhotoService photos(display, sdSpi);
TimeService clockTime;
BrightnessService brightness;
MessageService messageService;
NtfyClient ntfyClient;
OtaService* otaService = nullptr;
AlarmService alarmService;
AlarmAudio alarmAudio;
RtcLinkService rtcLink;
WavUploader wavUploader;

constexpr char kWavPath[] = "/clock/audio/alarm.wav";
constexpr int kOtaAlarmSafetyMinutes = 15;
uint32_t wavPendingBytes = 0;
char uploadPath[64] = {0};
uint32_t uploadPendingBytes = 0;
bool sdIsMounted = false;
HomeScreen homeScreen(display, unicodeText, photos, clockTime, messageService,
                      alarmService);
AlarmSettingsScreen alarmSettings(display, unicodeText);
RingingScreen ringingScreen(display, unicodeText);
MessagePopup messagePopup(display, unicodeText, messageService);
MessagesListScreen messagesListScreen(display, unicodeText, messageService,
                                      clockTime);
MessageDetailScreen messageDetail(display, unicodeText, messageService,
                                  clockTime);
EmojiService emojiService(display);

enum class ScreenMode : uint8_t {
  Home,
  AlarmSettings,
  Ringing,
  MessagesList,
  MessageDetail,
};
ScreenMode screenMode = ScreenMode::Home;

// Physical controls on the C3 coprocessor (pot/button/switch over the framed
// UART link). Tracked across loop() so we react to edges rather than levels.
// Volume is applied live on every change and persisted only after it has been
// stable for a while; the switch drives the hardware gate (never the stored
// config), and the button snoozes/dismisses.
uint8_t lastVolume = 0xFF;  // 0xFF == unknown yet
bool volumePersistPending = false;
uint32_t volumeChangedAtMs = 0;
bool volumeOverlayVisible = false;
uint8_t volumeOverlayPercent = 0;
uint32_t volumeOverlayStartMs = 0;
bool lastSwitchOn = false;
bool switchKnown = false;

char lastPopupMessageId[messagelogic::kMaxIdBytes] = {0};

bool touchIsStable = false;
bool touchIsEligible = false;
uint8_t pressedSamples = 0;
uint8_t releasedSamples = 0;
uint16_t touchDownX = 0;
uint16_t touchDownY = 0;
uint32_t touchDownAt = 0;
uint32_t lastTouchActionAt = 0;
uint32_t lastPhotoChangeAt = 0;
PhotoService::StartResult photoStartResult =
    PhotoService::StartResult::kInvalidManifest;

// Wi-Fi credentials loaded from the SD card. Held for the lifetime of the
// program because TimeService keeps pointers to these strings.
wificonfig::Credentials wifiCredentials;
ntfyconfig::Config* ntfyRuntimeConfig = nullptr;

const ntfyconfig::Config& loadNtfyConfig(bool sdMounted) {
  static const ntfyconfig::Config emptyConfig;
  if (ntfyRuntimeConfig == nullptr) {
    ntfyRuntimeConfig = new (std::nothrow) ntfyconfig::Config();
  }
  if (ntfyRuntimeConfig == nullptr) {
    Serial.println("[NTFY_CONFIG] allocation failed; client disabled");
    return emptyConfig;
  }
  *ntfyRuntimeConfig = ntfyconfig::Config{};
  if (!sdMounted) {
    Serial.println("[NTFY_CONFIG] missing; client disabled");
    return *ntfyRuntimeConfig;
  }
  File file = SD.open(ntfyconfig::kConfigPath, FILE_READ);
  if (!file) {
    Serial.println("[NTFY_CONFIG] missing; client disabled");
    return *ntfyRuntimeConfig;
  }
  if (file.size() > ntfyconfig::kMaxConfigBytes) {
    file.close();
    Serial.println("[NTFY_CONFIG] invalid; client disabled");
    return *ntfyRuntimeConfig;
  }
  char* buffer = new (std::nothrow) char[ntfyconfig::kMaxConfigBytes];
  if (buffer == nullptr) {
    file.close();
    Serial.println("[NTFY_CONFIG] allocation failed; client disabled");
    return *ntfyRuntimeConfig;
  }
  const std::size_t read = file.readBytes(buffer, ntfyconfig::kMaxConfigBytes);
  file.close();
  *ntfyRuntimeConfig = ntfyconfig::Parse(buffer, read);
  delete[] buffer;
  if (!ntfyRuntimeConfig->valid) {
    Serial.println("[NTFY_CONFIG] invalid; client disabled");
  } else {
    Serial.printf("[NTFY_CONFIG] loaded host_len=%u inbox_len=%u\n",
                  static_cast<unsigned>(std::strlen(ntfyRuntimeConfig->baseUrl)),
                  static_cast<unsigned>(std::strlen(ntfyRuntimeConfig->inboxTopic)));
  }
  return *ntfyRuntimeConfig;
}

const wificonfig::Credentials& loadWifiCredentials(bool sdMounted) {
  wifiCredentials = wificonfig::Credentials{};
  if (!sdMounted) {
    Serial.println("[WIFI] config missing; staying offline");
    return wifiCredentials;
  }
  File file = SD.open(wificonfig::kConfigPath, FILE_READ);
  if (!file) {
    Serial.println("[WIFI] config missing; staying offline");
    return wifiCredentials;
  }
  char buffer[wificonfig::kMaxConfigBytes];
  const std::size_t read = file.readBytes(buffer, sizeof(buffer));
  file.close();
  wifiCredentials = wificonfig::Parse(buffer, read);
  if (!wifiCredentials.valid) {
    Serial.println("[WIFI] config invalid; staying offline");
  } else {
    Serial.printf("[WIFI] config loaded ssid_len=%u\n",
                  static_cast<unsigned>(std::strlen(wifiCredentials.ssid)));
  }
  return wifiCredentials;
}

uint8_t touchTransfer(uint8_t output) {
  uint8_t input = 0;
  for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
    digitalWrite(pins::kTouchMosi, (output & mask) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(pins::kTouchClock, HIGH);
    input = static_cast<uint8_t>((input << 1) | digitalRead(pins::kTouchMiso));
    digitalWrite(pins::kTouchClock, LOW);
    delayMicroseconds(1);
  }
  return input;
}

uint16_t readTouchChannel(uint8_t command) {
  digitalWrite(pins::kTouchChipSelect, LOW);
  touchTransfer(command);
  const uint16_t value =
      static_cast<uint16_t>((touchTransfer(0) << 8) | touchTransfer(0));
  digitalWrite(pins::kTouchChipSelect, HIGH);
  return (value >> 3) & 0x0FFF;
}

bool readTouchPoint(uint16_t& screenX, uint16_t& screenY) {
  if (digitalRead(pins::kTouchInterrupt) != LOW) {
    return false;
  }
  uint32_t rawX = 0;
  uint32_t rawY = 0;
  constexpr uint8_t kSamples = 6;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    rawX += readTouchChannel(0xD0);
    rawY += readTouchChannel(0x90);
  }
  rawX /= kSamples;
  rawY /= kSamples;
  if (rawX < 100 || rawX > 4000 || rawY < 100 || rawY > 4000) {
    return false;
  }
  const int32_t mappedX =
      20 + (static_cast<int32_t>(rawY) - 429) * (299 - 20) / (3592 - 429);
  const int32_t mappedY =
      20 + (static_cast<int32_t>(rawX) - 510) * (219 - 20) / (3555 - 510);
  // The panel is mounted 180 degrees from the orientation this calibration was
  // captured for (display.setRotation(1)), so mirror both logical axes after
  // the existing raw-to-logical mapping instead of re-deriving the calibration.
  const int32_t clampedX = constrain(mappedX, 0, 319);
  const int32_t clampedY = constrain(mappedY, 0, 239);
  screenX = static_cast<uint16_t>(319 - clampedX);
  screenY = static_cast<uint16_t>(239 - clampedY);
  return true;
}

void runAudioDiagnostic();
void runFocusedAudioTest();
void runFrequencySweep();
void inspectAudioFiles();

void markPhotoChanged() { lastPhotoChangeAt = millis(); }

void showHome() {
  screenMode = ScreenMode::Home;
  messagePopup.dismiss();
  homeScreen.refresh();
  markPhotoChanged();
  Serial.printf("[SCREEN] home free_heap=%u\n", ESP.getFreeHeap());
}

void showRinging() {
  if (brightness.screenBlanked()) {
    brightness.wakeScreen();
    Serial.println("[DISPLAY] wake alarm");
  }
  screenMode = ScreenMode::Ringing;
  messagePopup.dismiss();
  const alarmclock::AlarmConfig& config = alarmService.config();
  const TimeService::Snapshot& now = clockTime.snapshot();
  ringingScreen.draw(now.valid, now.hour, now.minute, config.snoozeMinutes,
                      photoStartResult == PhotoService::StartResult::kReady);
  alarmAudio.start(config.volume);
  Serial.printf("[SCREEN] ringing free_heap=%u\n", ESP.getFreeHeap());
}

void showSnoozedHome(const char* inputLog) {
  alarmAudio.stop();
  showHome();
  Serial.println("[SNOOZE_UI] active");
  if (inputLog != nullptr) {
    Serial.println(inputLog);
  }
}

void showMessagesList() {
  screenMode = ScreenMode::MessagesList;
  messagePopup.dismiss();
  messagesListScreen.draw();
  Serial.printf("[SCREEN] messages_list free_heap=%u\n", ESP.getFreeHeap());
}

void showMessageDetail(std::size_t index) {
  screenMode = ScreenMode::MessageDetail;
  messagePopup.dismiss();
  messageDetail.open(index);
  messageDetail.draw();
  ntfyClient.requestAck();
  Serial.printf("[SCREEN] message_detail idx=%u free_heap=%u\n",
                static_cast<unsigned>(index), ESP.getFreeHeap());
}

void handleTap(uint16_t x, uint16_t y, uint32_t duration) {
  if (brightness.screenBlanked()) {
    brightness.wakeScreen();
    Serial.println("[DISPLAY] wake touch");
    return;
  }
  if (screenMode == ScreenMode::Ringing) {
    const RingingScreen::Action action = ringingScreen.handleTap(x, y);
    if (action == RingingScreen::Action::Snooze &&
        alarmService.snooze(clockTime.snapshot())) {
      showSnoozedHome("[INPUT] alarm snoozed");
    } else if (action == RingingScreen::Action::Stop &&
               alarmService.stop(clockTime.snapshot())) {
      alarmAudio.stop();
      showHome();
      Serial.println("[INPUT] alarm stopped");
    }
    return;
  }

  if (screenMode == ScreenMode::AlarmSettings) {
    const AlarmSettingsScreen::Action action = alarmSettings.handleTap(x, y);
    if (action == AlarmSettingsScreen::Action::Cancel) {
      showHome();
    } else if (action == AlarmSettingsScreen::Action::Save) {
      if (alarmService.applyConfig(alarmSettings.editConfig())) {
        showHome();
      } else {
        alarmSettings.setSaveError(true);
        alarmSettings.draw();
        Serial.println("[ALARM] settings save failed");
      }
    } else {
      alarmSettings.startRepeatTracking(millis());
      alarmSettings.draw();
    }
    return;
  }

  if (screenMode == ScreenMode::MessagesList) {
    const MessagesListScreen::Action action =
        messagesListScreen.handleTap(x, y);
    if (action == MessagesListScreen::Action::Back) {
      showHome();
    } else if (action == MessagesListScreen::Action::OpenDetail) {
      showMessageDetail(messagesListScreen.selectedIndex());
    } else {
      messagesListScreen.draw();
    }
    return;
  }

  if (screenMode == ScreenMode::MessageDetail) {
    const MessageDetailScreen::Action action = messageDetail.handleTap(x, y);
    if (action == MessageDetailScreen::Action::Back) {
      showHome();
    }
    return;
  }

  if (home::isHomeHeartTouch(x, y)) {
    brightness.blankScreen();
    Serial.println("[DISPLAY] blanked");
    return;
  }

  // Home: the popup (if any) takes precedence.
  if (messagePopup.active()) {
    showMessageDetail(messagePopup.messageIndex());
    return;
  }

  if (home::isSnoozeIndicatorTouch(
          x, y, alarmService.state() == alarmclock::AlarmState::Snoozed)) {
    if (alarmService.stop(clockTime.snapshot())) {
      alarmAudio.stop();
      showHome();
      Serial.println("[INPUT] snooze cancelled");
    }
    return;
  }

  if (x >= 260 && y <= 60) {
    Serial.printf(
        "[INPUT] touch x=%u y=%u duration_ms=%u action=messages\n",
        static_cast<unsigned>(x), static_cast<unsigned>(y), duration);
    showMessagesList();
    return;
  }
  if (x >= 210 && y >= 155) {
    Serial.printf(
        "[INPUT] touch x=%u y=%u duration_ms=%u action=alarm_settings\n",
        static_cast<unsigned>(x), static_cast<unsigned>(y), duration);
    alarmSettings.beginEdit(alarmService.config());
    alarmSettings.setHardwareAllowed(alarmService.hardwareAllowed());
    screenMode = ScreenMode::AlarmSettings;
    alarmSettings.draw();
    Serial.printf("[SCREEN] alarm_settings free_heap=%u\n", ESP.getFreeHeap());
    return;
  }
  if (photoStartResult != PhotoService::StartResult::kReady) {
    return;
  }

  const bool previous = x < display.width() / 2;
  Serial.printf("[INPUT] touch x=%u y=%u duration_ms=%u action=%s\n",
                static_cast<unsigned>(x), static_cast<unsigned>(y), duration,
                previous ? "previous" : "next");
  if (previous ? homeScreen.previousPhoto() : homeScreen.nextPhoto()) {
    markPhotoChanged();
  }
}

void handleTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool pressed = readTouchPoint(x, y);
  if (pressed) {
    releasedSamples = 0;
    if (touchIsStable) {
      return;
    }
    if (++pressedSamples < 3) {
      return;
    }
    pressedSamples = 0;
    touchIsStable = true;
    touchIsEligible = millis() - lastTouchActionAt >= kTouchCooldownMs;
    touchDownX = x;
    touchDownY = y;
    touchDownAt = millis();
    return;
  }

  pressedSamples = 0;
  if (!touchIsStable || ++releasedSamples < 4) {
    return;
  }
  releasedSamples = 0;
  touchIsStable = false;
  const uint32_t duration = millis() - touchDownAt;
  const uint32_t maximumDuration =
      screenMode == ScreenMode::Home ? kMaximumTapMs : 2500;
  if (!touchIsEligible || duration < kMinimumTapMs ||
      duration > maximumDuration) {
    return;
  }
  lastTouchActionAt = millis();
  handleTap(touchDownX, touchDownY, duration);
}

void drawVolumeOverlay() {
  if (brightness.screenBlanked()) {
    return;
  }
  const uint16_t bgColor =
      (screenMode == ScreenMode::Ringing) ? 0x0841 : kFallbackBackground;
  constexpr uint16_t kWarmWhite = 0xFF7B;
  constexpr uint16_t kCoral = 0xEBAC;

  // Home: upper-center photo region. Ringing: over the time region.
  const int16_t x = 80;
  const int16_t y = 55;
  const int16_t width = 160;
  const int16_t height = 38;

  display.fillRoundRect(x, y, width, height, 8, bgColor);
  display.drawRoundRect(x, y, width, height, 8, kCoral);

  char label[8];
  home::formatVolumePercent(volumeOverlayPercent, label, sizeof(label));
  unicodeText.setFont(u8g2_font_helvB14_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(kWarmWhite);
  unicodeText.drawUTF8(x + 10, y + 17, label);

  // Progress bar
  constexpr int16_t barX = x + 10;
  constexpr int16_t barY = y + 24;
  constexpr int16_t barWidth = width - 20;
  constexpr int16_t barHeight = 6;
  display.fillRoundRect(barX, barY, barWidth, barHeight, 3, 0x2104);
  const int16_t fillWidth =
      static_cast<int16_t>(barWidth * volumeOverlayPercent / 100);
  if (fillWidth > 0) {
    display.fillRoundRect(barX, barY, fillWidth, barHeight, 3, kCoral);
  }

  volumeOverlayVisible = true;
  volumeOverlayStartMs = millis();
}

void dismissVolumeOverlay() {
  if (!volumeOverlayVisible) {
    return;
  }
  volumeOverlayVisible = false;
  if (screenMode == ScreenMode::Home) {
    homeScreen.refresh();
  } else if (screenMode == ScreenMode::Ringing) {
    const TimeService::Snapshot& now = clockTime.snapshot();
    ringingScreen.refreshCurrentTime(now.valid, now.hour, now.minute);
  }
}

// Reacts to the physical controls on the C3 coprocessor (pot, button, switch)
// as framed frames arrive over the RTC link. Volume applies live and persists
// only after stability; the switch toggles the hardware gate (never the stored
// config); the button snoozes a ringing alarm and otherwise dismisses to home.
void handlePhysicalInputs() {
  if (rtcLink.volumeKnown()) {
    const uint8_t volume = rtcLink.volumePercent();
    if (volume != lastVolume) {
      lastVolume = volume;
      alarmAudio.setVolume(volume);
      volumeChangedAtMs = millis();
      volumePersistPending = true;
      Serial.printf("[INPUT] volume=%u\n", static_cast<unsigned>(volume));

      if (!brightness.screenBlanked() &&
          (screenMode == ScreenMode::Home || screenMode == ScreenMode::Ringing)) {
        volumeOverlayPercent = volume;
        drawVolumeOverlay();
      }
    }
  }

  if (rtcLink.alarmSwitchKnown()) {
    const bool on = rtcLink.alarmSwitchOn();
    if (!switchKnown || on != lastSwitchOn) {
      switchKnown = true;
      lastSwitchOn = on;
      if (!on) {
        // Switch OFF: first resolve the current occurrence while the gate still
        // allows stop() to run, stop audio and return to Home, then close the
        // physical gate. Never touch the stored config (time/days/enable).
        if (alarmService.state() == alarmclock::AlarmState::Ringing ||
            alarmService.state() == alarmclock::AlarmState::Snoozed) {
          alarmService.stop(clockTime.snapshot());
          alarmAudio.stop();
          if (screenMode == ScreenMode::Ringing) {
            showHome();
          }
        }
        alarmService.setHardwareAllowed(false);
        Serial.println("[INPUT] switch=off alarms_blocked");
      } else {
        alarmService.setHardwareAllowed(true);
        Serial.println("[INPUT] switch=on alarms_allowed");
      }
      // Give the physical switch immediate visual feedback on the active screen.
      if (screenMode == ScreenMode::Home) {
        homeScreen.refreshAlarm();
      } else if (screenMode == ScreenMode::AlarmSettings) {
        alarmSettings.setHardwareAllowed(alarmService.hardwareAllowed());
      }
    }
  }

  const std::uint32_t snoozeCount = rtcLink.consumeSnoozePressed();
  for (std::uint32_t index = 0; index < snoozeCount; ++index) {
    if (screenMode == ScreenMode::Ringing) {
      if (alarmService.snooze(clockTime.snapshot())) {
        showSnoozedHome("[INPUT] button snooze");
      }
    } else {
      showHome();
      Serial.println("[INPUT] button dismiss");
    }
  }
}

bool parseUnsigned(const char* value, unsigned long maximum,
                   unsigned long& parsed) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long result = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || result > maximum) {
    return false;
  }
  parsed = result;
  return true;
}

void printCommands() {
  Serial.println(
      "[COMMANDS] !n !p !r photos, !t home test, !x failure test, "
      "!d diagnostics, !q audio test, !m COUNT, !b 0..255, !v 0..100, "
      "!alarm [...], !wavraw BYTES, "
      "!msg [list|unread|read INDEX|ack|inject <text>|processed <id>|clear], "
      "!ntfy, !ota [status|check|update], !wifi [status|scan SSID], !rtc [sync], "
      "!c3, !c3 ping, !emoji, !gift-reset, !gift-reset-full, "
      "!ls [dir], !rmfile <path>, !wificfg get");
}

int minutesUntilNextScheduledAlarm() {
  const TimeService::Snapshot& now = clockTime.snapshot();
  const alarmclock::AlarmConfig& config = alarmService.config();
  if (!now.valid || !config.softwareEnabled || !alarmService.hardwareAllowed()) {
    return 7 * 24 * 60;
  }
  const int currentMinutes = now.hour * 60 + now.minute;
  const int alarmMinutes = config.hour * 60 + config.minute;
  for (int dayOffset = 0; dayOffset <= 7; ++dayOffset) {
    const int weekday = (now.weekday + dayOffset) % 7;
    const uint8_t bit = static_cast<uint8_t>(1U << ((weekday + 6) % 7));
    if ((config.daysMask & bit) == 0) continue;
    const int delta = dayOffset * 24 * 60 + alarmMinutes - currentMinutes;
    if (delta >= 0) return delta;
  }
  return 7 * 24 * 60;
}

void refreshHomeIfVisible() {
  if (screenMode == ScreenMode::Home) {
    homeScreen.refresh();
  }
}

void drainSerial() {
  while (Serial.available() > 0) {
    Serial.read();
  }
  delay(50);
  while (Serial.available() > 0) {
    Serial.read();
  }
}

void drawWavStatus() {
  display.fillScreen(kFallbackBackground);
  unicodeText.setFont(u8g2_font_9x15_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(kWarmWhite);
  constexpr char title[] = "Prenašam zvok budilke";
  const int16_t titleWidth = unicodeText.getUTF8Width(title);
  unicodeText.drawUTF8((display.width() - titleWidth) / 2, 100, title);
  unicodeText.setFont(u8g2_font_6x12_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(0xAD55);
  constexpr char hint[] = "Ne odklapljaj USB";
  const int16_t hintWidth = unicodeText.getUTF8Width(hint);
  unicodeText.drawUTF8((display.width() - hintWidth) / 2, 130, hint);
}

void printSdInfo() {
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  Serial.printf("[SD_INFO] total=%llu used=%llu free=%llu audio_dir=%u "
                "audio_file=%u\n", total, used, total - used,
                static_cast<unsigned>(SD.exists("/clock/audio")),
                static_cast<unsigned>(SD.exists(kWavPath)));
}

void printWavResult(WavUploader::Result result) {
  switch (result) {
    case WavUploader::Result::kSuccess:
      Serial.printf("[WAV_UPLOAD] done path=%s\n", kWavPath);
      break;
    case WavUploader::Result::kCrcError:
      Serial.printf("[WAV_UPLOAD] fail reason=crc path=%s\n", kWavPath);
      break;
    case WavUploader::Result::kSdError:
      Serial.printf("[WAV_UPLOAD] fail reason=sd path=%s\n", kWavPath);
      break;
    case WavUploader::Result::kTimeout:
      Serial.printf("[WAV_UPLOAD] fail reason=timeout path=%s\n", kWavPath);
      break;
    case WavUploader::Result::kSizeError:
      Serial.printf("[WAV_UPLOAD] fail reason=size path=%s\n", kWavPath);
      break;
    case WavUploader::Result::kFinalizeError:
      Serial.printf("[WAV_UPLOAD] fail reason=finalize path=%s\n", kWavPath);
      break;
  }
}

namespace {

struct FullResetCounts {
  unsigned long removed = 0;
  unsigned long photosKept = 0;
  unsigned long manifestKept = 0;
};

// Recursively delete every entry under 'directory' except fullreset-protected
// paths. Returns true when protected content remains in the directory, so the
// caller must not remove the directory itself.
//
// Deleting entries while a FAT directory iterator is active can skip adjacent
// entries; survivors are swept by bounded follow-up passes.
bool wipeSdDirectory(const char* directory, FullResetCounts& counts,
                     int pass = 0) {
  File handle = SD.open(directory, FILE_READ);
  if (!handle || !handle.isDirectory()) {
    if (handle) handle.close();
    return false;
  }
  bool hasProtected = false;
  bool removedAnyThisPass = false;
  unsigned long yieldCounter = 0;
  for (File entry = handle.openNextFile(); entry;
       entry = handle.openNextFile()) {
    // The delete sweep can run for many seconds; yield periodically so the
    // task watchdog of the loop() task does not reset the board mid-wipe.
    if ((++yieldCounter & 0x3F) == 0) {
      delay(1);
    }
    char path[128];
    const char* name = entry.name();
    if (name[0] == '/') {
      std::snprintf(path, sizeof(path), "%s", name);
    } else if (std::strcmp(directory, "/") == 0) {
      std::snprintf(path, sizeof(path), "/%s", name);
    } else {
      std::snprintf(path, sizeof(path), "%s/%s", directory, name);
    }
    if (fullreset::isProtectedPath(path)) {
      hasProtected = true;
      if (fullreset::isPhotoPath(path)) ++counts.photosKept;
      if (fullreset::isManifestPath(path)) ++counts.manifestKept;
      entry.close();
      continue;
    }
    if (entry.isDirectory()) {
      const bool childProtected = wipeSdDirectory(path, counts);
      entry.close();
      if (childProtected) {
        hasProtected = true;
      } else if (SD.rmdir(path)) {
        ++counts.removed;
        removedAnyThisPass = true;
      } else {
        hasProtected = true;
      }
      continue;
    }
    if (SD.remove(path)) {
      ++counts.removed;
      removedAnyThisPass = true;
    }
    entry.close();
  }
  handle.close();
  if (removedAnyThisPass && pass < 5) {
    return wipeSdDirectory(directory, counts, pass + 1) || hasProtected;
  }
  return hasProtected;
}

}  // namespace

void executeAlarmCommand(const char* command) {
  const alarmclock::AlarmSerialCommand parsed =
      alarmclock::parseAlarmSerialCommand(command);
  alarmclock::AlarmConfig next = alarmService.config();
  bool saved = true;
  switch (parsed.type) {
    case alarmclock::AlarmSerialCommandType::Status:
      alarmService.printStatus();
      alarmAudio.printStatus();
      return;
    case alarmclock::AlarmSerialCommandType::Enable:
      saved = alarmService.setEnabled(true);
      break;
    case alarmclock::AlarmSerialCommandType::Disable:
      saved = alarmService.setEnabled(false);
      alarmAudio.stop();
      if (screenMode == ScreenMode::Ringing) {
        showHome();
      }
      break;
    case alarmclock::AlarmSerialCommandType::SetTime:
      next.hour = parsed.hour;
      next.minute = parsed.minute;
      saved = alarmService.applyConfig(next);
      break;
    case alarmclock::AlarmSerialCommandType::SetDays:
      saved = alarmService.setDays(parsed.daysMask);
      break;
    case alarmclock::AlarmSerialCommandType::SetSnoozeMinutes:
      saved = alarmService.setSnoozeMinutes(parsed.snoozeMinutes);
      break;
    case alarmclock::AlarmSerialCommandType::Test:
      alarmService.testRing(clockTime.snapshot());
      showRinging();
      return;
    case alarmclock::AlarmSerialCommandType::Snooze:
      if (alarmService.snooze(clockTime.snapshot())) {
        showSnoozedHome(nullptr);
      } else {
        Serial.println("[ALARM] snooze rejected");
      }
      return;
    case alarmclock::AlarmSerialCommandType::Stop:
      if (alarmService.stop(clockTime.snapshot())) {
        alarmAudio.stop();
        showHome();
      } else {
        Serial.println("[ALARM] stop rejected");
      }
      return;
    case alarmclock::AlarmSerialCommandType::ResetHandledDay:
      if (alarmService.resetHandledDay()) {
        alarmService.printStatus();
      } else {
        Serial.println("[ALARM] settings save failed");
      }
      return;
    case alarmclock::AlarmSerialCommandType::Invalid:
      Serial.println("[ALARM] invalid command");
      return;
  }
  if (!saved) {
    Serial.println("[ALARM] settings save failed");
    return;
  }
  refreshHomeIfVisible();
  alarmService.printStatus();
}

void executeCommand(const char* command) {
  if (command[0] == '\0') {
    return;
  }
  if (strncmp(command, "wifi", 4) == 0 &&
      (command[4] == '\0' || command[4] == ' ')) {
    const char* action = command + 4;
    while (*action == ' ') ++action;
    if (*action == '\0' || std::strcmp(action, "status") == 0) {
      const bool connected = WiFi.status() == WL_CONNECTED;
      Serial.printf("[WIFI_STATUS] connected=%s status=%u rssi=%d ip=%s\n",
                    connected ? "yes" : "no",
                    static_cast<unsigned>(WiFi.status()),
                    connected ? WiFi.RSSI() : 0,
                    connected ? WiFi.localIP().toString().c_str() : "none");
      return;
    }
    if (strncmp(action, "scan ", 5) == 0 && action[5] != '\0') {
      const char* target = action + 5;
      const int count = WiFi.scanNetworks(false, true);
      bool found = false;
      int bestRssi = -127;
      int channel = 0;
      int auth = 0;
      for (int index = 0; index < count; ++index) {
        if (WiFi.SSID(index) == target && WiFi.RSSI(index) > bestRssi) {
          found = true;
          bestRssi = WiFi.RSSI(index);
          channel = WiFi.channel(index);
          auth = static_cast<int>(WiFi.encryptionType(index));
        }
      }
      WiFi.scanDelete();
      Serial.printf("[WIFI_SCAN] target_len=%u visible=%s rssi=%d channel=%d auth=%d\n",
                    static_cast<unsigned>(std::strlen(target)),
                    found ? "yes" : "no", found ? bestRssi : 0,
                    found ? channel : 0, found ? auth : -1);
      return;
    }
    Serial.println("[WIFI] usage: !wifi status|scan SSID");
    return;
  }
  if (command[1] == '\0') {
    switch (command[0]) {
      case 'n':
      case 'N':
        if (photoStartResult == PhotoService::StartResult::kReady &&
            homeScreen.nextPhoto()) {
          markPhotoChanged();
        }
        return;
      case 'p':
      case 'P':
        if (photoStartResult == PhotoService::StartResult::kReady &&
            homeScreen.previousPhoto()) {
          markPhotoChanged();
        }
        return;
      case 'r':
      case 'R':
        if (photoStartResult == PhotoService::StartResult::kReady &&
            homeScreen.randomPhoto()) {
          markPhotoChanged();
        }
        return;
      case 't':
      case 'T':
        if (photoStartResult != PhotoService::StartResult::kReady) {
          homeScreen.showInitial();
          return;
        }
        Serial.println("[HOME_TEST] starting 10 sequential Home renders");
        for (uint8_t i = 0; i < 10; ++i) {
          if (!homeScreen.nextPhoto()) {
            break;
          }
          delay(20);
        }
        markPhotoChanged();
        photos.printStats();
        Serial.println("[HOME_TEST] complete");
        return;
      case 'x':
      case 'X':
        if (photoStartResult == PhotoService::StartResult::kReady) {
          photos.runFailureSelfTest();
          homeScreen.refresh();
          photos.printStats();
        }
        return;
      case 'e':
      case 'E':
        homeScreen.showLibraryError();
        Serial.println("[HOME_TEST] library error screen displayed");
        return;
      case 'd':
      case 'D':
        photos.printStats();
        printSdInfo();
        clockTime.printStatus();
        brightness.printStatus();
        alarmService.printStatus();
        alarmAudio.printStatus();
        Serial.printf("[SCREEN] mode=%u free_heap=%u\n",
                      static_cast<unsigned>(screenMode), ESP.getFreeHeap());
        return;
      case 'h':
      case 'H':
        printCommands();
        return;
      case 'q':
        runAudioDiagnostic();
        return;
      case 'Q':
        runFocusedAudioTest();
        return;
      case 's':
      case 'S':
        runFrequencySweep();
        return;
      case 'i':
      case 'I':
        inspectAudioFiles();
        return;
      default:
        return;
    }
  }

  if (strncmp(command, "msg", 3) == 0 &&
      (command[3] == '\0' || command[3] == ' ')) {
    if (command[3] == '\0') {
      messageService.printStatus();
      ntfyClient.printStatus();
      return;
    }
    if (strncmp(command + 4, "list", 4) == 0 &&
        (command[8] == '\0' || command[8] == ' ')) {
      messageService.printList();
      return;
    }
    if (strncmp(command + 4, "unread", 6) == 0 &&
        (command[10] == '\0' || command[10] == ' ')) {
      const MessageStatus status = messageService.status();
      Serial.printf("[MSGRO] unread=%u count=%u\n",
                    static_cast<unsigned>(status.unreadCount),
                    static_cast<unsigned>(messageService.count()));
      return;
    }
    if (strncmp(command + 4, "ack", 3) == 0 &&
        (command[7] == '\0' || command[7] == ' ')) {
      ntfyClient.requestAck();
      Serial.println("[MSGRO] ack queued");
      return;
    }
    if (strncmp(command + 4, "read", 4) == 0 && command[8] == ' ') {
      unsigned long index = 0;
      if (parseUnsigned(command + 9, 100, index) &&
          messageService.markReadAtIndex(index)) {
        ntfyClient.requestAck();
        Serial.printf("[MSGRO] marked index %lu read (ack queued)\n", index);
      } else {
        Serial.println("[MSGRO] read failed");
      }
      return;
    }
    if (strncmp(command + 4, "inject ", 7) == 0 && command[11] != '\0') {
      // Build a synthetic ntfy JSON event and funnel it through the same
      // ingest path the network stream uses.
      messagelogic::InboxMessage injected;
      char line[messagelogic::kMaxLineBytes];
      const std::int64_t now = clockTime.snapshot().valid
                                   ? clockTime.snapshot().epochSeconds
                                   : 0;
      const int written = std::snprintf(
          line, sizeof(line),
          "{\"id\":\"inject%lld\",\"time\":%lld,\"event\":\"message\","
          "\"title\":\"Luka\",\"message\":\"%s\"}",
          static_cast<long long>(now), static_cast<long long>(now),
          command + 11);
      if (written > 0 &&
          messagelogic::preIngestLine(line, written, injected) ==
              messagelogic::IngestResult::kAccepted) {
        const MessageService::AddResult added =
            messageService.addIncoming(injected);
        if (added == MessageService::AddResult::kAccepted) {
          messageService.saveProcessedId(injected.id);
          showHome();
          Serial.printf("[MSGRO] injected id=%s\n", injected.id);
        } else {
          Serial.println("[MSGRO] inject failed");
        }
        return;
      }
      Serial.println("[MSGRO] invalid inject text");
      return;
    }
    if (strncmp(command + 4, "processed", 9) == 0 &&
        (command[13] == '\0' || command[13] == ' ')) {
      if (command[13] == ' ') {
        const char* rest = command + 14;
        if (std::strcmp(rest, "clear") == 0) {
          if (messageService.resetProcessedId()) {
            Serial.println("[MSGRO] processed cleared");
          } else {
            Serial.println("[MSGRO] processed clear failed");
          }
          return;
        }
        const std::size_t idLength = std::strlen(rest);
        bool printable = idLength > 0;
        for (std::size_t index = 0; printable && index < idLength; ++index) {
          const char value = rest[index];
          if (value < 0x20 || value > 0x7E) {
            printable = false;
          }
        }
        if (printable && idLength < messagelogic::kMaxIdBytes &&
            messageService.saveProcessedId(rest)) {
          Serial.printf("[MSGRO] processed set id=%s\n", rest);
        } else {
          Serial.println("[MSGRO] processed set failed");
        }
        return;
      }
      messageService.printStatus();
      return;
    }
    Serial.println(
        "[MSGRO_CMD] usage: !msg, !msg list, !msg unread, !msg read INDEX, "
        "!msg ack, !msg inject <text>, !msg processed <id>|clear");
    return;
  }
  if (strncmp(command, "gift-reset", 10) == 0 &&
      (command[10] == '\0' || command[10] == ' ')) {
    // Clearing the persisted alarm/config state is NOT part of this command.
    // It removes user/test state only:
    //   - all local messages (read/unread/seen metadata lives inside the
    //     store lines, so removing the store clears them together)
    //   - any Ringing / Snoozed / test-alarm transient engine state
    //   - the persisted handled_occurrence
    // The ntfy checkpoint (processed.json) is preserved so the stream resumes
    // after the last accepted message without replaying retained ones.
    const bool messagesCleared = messageService.clearAll();
    if (alarmService.state() == alarmclock::AlarmState::Ringing ||
        alarmService.state() == alarmclock::AlarmState::Snoozed) {
      alarmService.stop(clockTime.snapshot());
    }
    alarmAudio.stop();
    const bool handledCleared = alarmService.resetHandledDay();
    showHome();
    const MessageStatus status = messageService.status();
    Serial.printf(
        "[GIFT_RESET] messages=%u unread=%u handled=%s state=%s origin=%s "
        "messages_cleared=%s handled_cleared=%s\n",
        static_cast<unsigned>(messageService.count()),
        static_cast<unsigned>(status.unreadCount),
        alarmService.handledOccurrence() == 0 ? "none" : "set",
        "armed", "none", messagesCleared ? "yes" : "no",
        handledCleared ? "yes" : "no");
    return;
  }
  if (strncmp(command, "gift-reset-full", 15) == 0 &&
      (command[15] == '\0' || command[15] == ' ')) {
    if (!sdIsMounted) {
      Serial.println("[FULL_RESET] fail reason=sd");
      return;
    }
    // Full reset is an SD rebuild only. Stop alarm audio and resolve any
    // transient ringing state; flash, C3, RTC and calibration are untouched.
    if (alarmService.state() == alarmclock::AlarmState::Ringing ||
        alarmService.state() == alarmclock::AlarmState::Snoozed) {
      alarmService.stop(clockTime.snapshot());
    }
    alarmAudio.stop();
    FullResetCounts counts;
    wipeSdDirectory("/", counts);
    refreshHomeIfVisible();
    Serial.printf(
        "[FULL_RESET] removed=%lu kept=%lu photos_kept=%lu "
        "manifest_kept=%lu\n",
        counts.removed, counts.photosKept + counts.manifestKept,
        counts.photosKept, counts.manifestKept);
    return;
  }
  if (strncmp(command, "ntfy", 4) == 0) {
    const char* rest = command + 4;
    if (*rest == '\0' || *rest == ' ') {
      if (*rest == ' ' && strncmp(rest + 1, "reconnect", 9) == 0 &&
          (rest[10] == '\0' || rest[10] == ' ')) {
        ntfyClient.reconnectNow();
        Serial.println("[NTFY] reconnect scheduled");
        return;
      }
      ntfyClient.printStatus();
      return;
    }
    Serial.println("[NTFY] usage: !ntfy, !ntfy reconnect");
    return;
  }
  if (strncmp(command, "ota", 3) == 0 &&
      (command[3] == '\0' || command[3] == ' ')) {
    if (otaService == nullptr) {
      Serial.println("[OTA] unavailable");
      return;
    }
    const char* action = command[3] == ' ' ? command + 4 : "status";
    if (std::strcmp(action, "status") == 0) {
      otaService->printStatus();
    } else if (std::strcmp(action, "check") == 0) {
      Serial.printf("[OTA] check request=%s\n",
                    otaService->requestCheck() ? "accepted" : "rejected");
    } else if (std::strcmp(action, "update") == 0) {
      Serial.printf("[OTA] update request=%s\n",
                    otaService->requestUpdate() ? "accepted" : "rejected");
    } else {
      Serial.println("[OTA] usage: !ota status|check|update");
    }
    return;
  }
  if (strncmp(command, "c3", 2) == 0) {
    const char* rest = command + 2;
    if (*rest == ' ' && strncmp(rest + 1, "ping", 4) == 0 &&
        (rest[5] == '\0' || rest[5] == ' ')) {
      rtcLink.requestStatus();
      Serial.println("[C3] status request queued");
      return;
    }
    Serial.printf(
        "[C3] link=%s compat=%s rtc=%s epoch=%u switch=%s volume=%s "
        "last_seen_ms=%lld\n",
        rtcLink.connected() ? "connected" : "down",
        rtcLink.protocolCompatible() ? "yes" : "no",
        rtcLink.rtcValid() ? "yes" : "no",
        static_cast<unsigned>(rtcLink.rtcEpoch()),
        rtcLink.alarmSwitchKnown()
            ? (rtcLink.alarmSwitchOn() ? "on" : "off")
            : "none",
        rtcLink.volumeKnown() ? "yes" : "no",
        static_cast<long long>(rtcLink.lastSeenMs()));
    return;
  }
  if (strncmp(command, "rtc", 3) == 0) {
    const char* rest = command + 3;
    if (*rest == ' ' && strncmp(rest + 1, "sync", 4) == 0 &&
        (rest[5] == '\0' || rest[5] == ' ')) {
      if (!clockTime.snapshot().valid) {
        Serial.println("[RTC] no trusted time to sync yet");
      } else {
        rtcLink.forceSync(clockTime.snapshot().epochSeconds);
        Serial.println("[RTC] sync queued");
      }
      return;
    }
    clockTime.printStatus();
    rtcLink.printStatus();
    Serial.printf("[RTC] clock_source=%s\n",
                  clockTime.source() == timesource::Source::kNtp
                      ? "ntp"
                      : clockTime.source() == timesource::Source::kRtc
                            ? "ds1302"
                            : "none");
    return;
  }
  if (strcmp(command, "emoji") == 0) {
    constexpr uint16_t kBg = 0x1082;
    display.fillRect(0, 40, 320, 180, kBg);
    struct { const char* ch; int16_t x; } tests[] = {
      {"❤", 10}, {"😊", 70}, {"🥰", 130}, {"😘", 190}, {"👍", 250}
    };
    uint32_t t0 = millis();
    for (auto& t : tests) {
      emojiService.draw(t.ch, t.x, 50, 48);
    }
    uint32_t t1 = millis();
    Serial.printf("[EMOJI] 5 emojis at 48px = %u ms\n",
                  static_cast<unsigned>(t1 - t0));
    return;
  }
  if (strncmp(command, "alarm", 5) == 0) {
    executeAlarmCommand(command);
    return;
  }
  if (strncmp(command, "wavraw ", 7) == 0 && command[7] != '\0') {
    if (!sdIsMounted) {
      Serial.println("[WAV_UPLOAD] fail reason=sd path=");
      return;
    }
    unsigned long byteCount = 0;
    if (!parseUnsigned(command + 7, 8UL * 1024UL * 1024UL, byteCount) ||
        byteCount < 100) {
      Serial.println("[WAV_UPLOAD] fail reason=size path=");
      return;
    }
    if (wavPendingBytes != 0) {
      Serial.println("[WAV_UPLOAD] fail reason=busy path=");
      return;
    }
    drainSerial();
    wavPendingBytes = static_cast<uint32_t>(byteCount);
    Serial.printf("WAVREADY %lu\n", byteCount);
    Serial.flush();
    return;
  }
  if (command[0] == 'v' && command[1] == ' ') {
    unsigned long value = 0;
    if (parseUnsigned(command + 2, 100, value) &&
        alarmService.setVolume(static_cast<uint8_t>(value))) {
      alarmAudio.setVolume(static_cast<uint8_t>(value));
      alarmService.printStatus();
    } else {
      Serial.println("[ALARM] invalid volume or save failed");
    }
    return;
  }
  if (command[0] == 'b' && command[1] == ' ') {
    unsigned long value = 0;
    if (parseUnsigned(command + 2, UINT8_MAX, value)) {
      brightness.set(static_cast<uint8_t>(value));
      brightness.printStatus();
    } else {
      Serial.println("[BRIGHTNESS] invalid value");
    }
    return;
  }
  if (strncmp(command, "putfile ", 8) == 0) {
    // !putfile <path> <byteCount>
    const char* pathArg = command + 8;
    const char* space = std::strchr(pathArg, ' ');
    if (space != nullptr && space > pathArg) {
      unsigned long byteCount = 0;
      if (parseUnsigned(space + 1, 128UL * 1024UL, byteCount) && byteCount > 0) {
        const size_t pathLen = static_cast<size_t>(space - pathArg);
        if (pathLen < sizeof(uploadPath)) {
          std::memcpy(uploadPath, pathArg, pathLen);
          uploadPath[pathLen] = '\0';
          uploadPendingBytes = static_cast<uint32_t>(byteCount);
          drainSerial();
          Serial.printf("FILEREADY %lu\n", byteCount);
          Serial.flush();
          return;
        }
      }
    }
  }
  if (strncmp(command, "mkdir ", 6) == 0 && command[6] != '\0') {
    if (sdIsMounted) {
      Serial.printf(SD.mkdir(command + 6) ? "[MKDIR] created %s\n" : "[MKDIR] failed %s\n",
                    command + 6);
    } else {
      Serial.println("[MKDIR] sd not available");
    }
    return;
  }
  if (strncmp(command, "ls", 2) == 0 &&
      (command[2] == '\0' || command[2] == ' ')) {
    if (!sdIsMounted) {
      Serial.println("[LS] sd not available");
      return;
    }
    const char* dirPath = (command[2] == ' ') ? command + 3 : "/";
    File directory = SD.open(dirPath, FILE_READ);
    if (!directory || !directory.isDirectory()) {
      Serial.printf("[LS] open failed path=%s\n", dirPath);
      if (directory) directory.close();
      return;
    }
    uint16_t count = 0;
    for (File entry = directory.openNextFile(); entry;
         entry = directory.openNextFile()) {
      char path[128];
      const char* name = entry.name();
      if (name[0] == '/') {
        std::snprintf(path, sizeof(path), "%s", name);
      } else if (std::strcmp(dirPath, "/") == 0) {
        std::snprintf(path, sizeof(path), "/%s", name);
      } else {
        std::snprintf(path, sizeof(path), "%s/%s", dirPath, name);
      }
      if (entry.isDirectory()) {
        Serial.printf("[LS] %s/\n", path);
      } else {
        Serial.printf("[LS] %s bytes=%u\n", path,
                      static_cast<unsigned>(entry.size()));
      }
      entry.close();
      ++count;
    }
    directory.close();
    Serial.printf("[LS_END] dir=%s count=%u\n", dirPath,
                  static_cast<unsigned>(count));
    return;
  }
  if (strncmp(command, "rmfile ", 7) == 0 && command[7] != '\0') {
    if (!sdIsMounted) {
      Serial.println("[RMFILE] sd not available");
      return;
    }
    const char* path = command + 7;
    const std::size_t pathLength = std::strlen(path);
    const bool emojiPath = pathLength > 7 &&
                           std::strncmp(path, "/emoji/", 7) == 0;
    const bool pngFile = pathLength > 4 &&
                         std::strcmp(path + pathLength - 4, ".png") == 0;
    if (!emojiPath || !pngFile) {
      Serial.printf("[RMFILE] denied path=%s\n", path);
      return;
    }
    Serial.printf(SD.remove(path) ? "[RMFILE] removed %s\n"
                                  : "[RMFILE] failed path=%s\n",
                  path);
    return;
  }
  if (strncmp(command, "wificfg ", 8) == 0) {
    // Maintenance-only helper for tools/gift_reset.py, which needs to capture
    // /clock/config/wifi.json before a full wipe. No arbitrary path is
    // accepted; only this exact file can be read. The dump is hex-encoded so
    // the Wi-Fi password never appears as plain text in the serial stream.
    if (std::strcmp(command + 8, "get") == 0) {
      if (!sdIsMounted) {
        Serial.println("[WIFICFG] missing");
        return;
      }
      File file = SD.open(wificonfig::kConfigPath, FILE_READ);
      if (!file) {
        Serial.println("[WIFICFG] missing");
        return;
      }
      Serial.printf("[WIFICFG] bytes=%u\n",
                    static_cast<unsigned>(file.size()));
      static const char kDigits[] = "0123456789abcdef";
      constexpr std::size_t kHexLineBytes = 64;
      char hex[kHexLineBytes * 2 + 1];
      std::size_t used = 0;
      while (file.available() > 0) {
        const int value = file.read();
        if (value < 0) {
          break;
        }
        hex[used++] = kDigits[(value >> 4) & 0x0F];
        hex[used++] = kDigits[value & 0x0F];
        if (used == kHexLineBytes * 2) {
          hex[used] = '\0';
          Serial.println(hex);
          used = 0;
        }
      }
      file.close();
      if (used > 0) {
        hex[used] = '\0';
        Serial.println(hex);
      }
      Serial.println("[WIFICFG] end");
      return;
    }
    Serial.println("[WIFICFG] usage: !wificfg get");
    return;
  }
}

void handleSerial() {
  static char buffer[64]{};
  static uint8_t length = 0;
  static bool discarding = false;
  while (Serial.available() > 0) {
    const char received = static_cast<char>(Serial.read());
    if (received == '\r') {
      continue;
    }
    if (received == '\n') {
      if (!discarding && length >= 2 && buffer[0] == '!') {
        buffer[length] = '\0';
        executeCommand(buffer + 1);
      }
      length = 0;
      discarding = false;
    } else if (discarding) {
      continue;
    } else if (received >= 0x20 && received <= 0x7E &&
               length < sizeof(buffer) - 1) {
      buffer[length++] = received;
    } else {
      length = 0;
      discarding = true;
    }
  }
}

void initializeTouch() {
  pinMode(pins::kTouchMiso, INPUT);
  pinMode(pins::kTouchMosi, OUTPUT);
  pinMode(pins::kTouchClock, OUTPUT);
  pinMode(pins::kTouchChipSelect, OUTPUT);
  pinMode(pins::kTouchInterrupt, INPUT);
  digitalWrite(pins::kTouchClock, LOW);
  digitalWrite(pins::kTouchChipSelect, HIGH);
}

void drawLoadingScreen() {
  display.fillScreen(kFallbackBackground);
  unicodeText.setFont(u8g2_font_9x15_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(kWarmWhite);
  constexpr char message[] = "Nalagam fotografije ...";
  const int16_t width = unicodeText.getUTF8Width(message);
  unicodeText.drawUTF8((display.width() - width) / 2, 124, message);
}

void drawAudioTestStep(uint8_t step, const char* name) {
  display.fillScreen(0x0000);
  char number[2] = {static_cast<char>('0' + step), '\0'};
  unicodeText.setFont(u8g2_font_logisoso92_tn);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(kWarmWhite);
  const int16_t numberWidth = unicodeText.getUTF8Width(number);
  unicodeText.drawUTF8((display.width() - numberWidth) / 2, 126, number);
  unicodeText.setFont(u8g2_font_9x15_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(0xEBAC);
  const int16_t nameWidth = unicodeText.getUTF8Width(name);
  unicodeText.drawUTF8(max<int16_t>(8, (display.width() - nameWidth) / 2), 180,
                       name);
  unicodeText.setFont(u8g2_font_6x12_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(0xAD55);
  constexpr char instruction[] = "ZVOK 4 SEKUNDE";
  const int16_t instructionWidth = unicodeText.getUTF8Width(instruction);
  unicodeText.drawUTF8((display.width() - instructionWidth) / 2, 218,
                       instruction);
  Serial.printf("[AUDIO_TEST] step=%u method=%s starting\n",
                static_cast<unsigned>(step), name);
  delay(1500);
}

void audioTestSilence() {
  noTone(pins::kAudio);
  ledcWriteTone(0, 0);
  ledcWriteTone(6, 0);
  ledcDetachPin(pins::kAudio);
  dacDisable(pins::kAudio);
  pinMode(pins::kAudio, OUTPUT);
  digitalWrite(pins::kAudio, LOW);
  display.fillScreen(0x0000);
  unicodeText.setFont(u8g2_font_helvB14_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(0xAD55);
  constexpr char label[] = "TIŠINA";
  const int16_t width = unicodeText.getUTF8Width(label);
  unicodeText.drawUTF8((display.width() - width) / 2, 130, label);
  delay(2000);
}

void runAudioDiagnostic() {
  alarmAudio.stop();
  const uint32_t stageMs = 4000;

  drawAudioTestStep(1, "digitalWrite 500 Hz");
  uint32_t startedAt = millis();
  while (millis() - startedAt < stageMs) {
    digitalWrite(pins::kAudio, HIGH);
    delayMicroseconds(1000);
    digitalWrite(pins::kAudio, LOW);
    delayMicroseconds(1000);
  }
  audioTestSilence();

  drawAudioTestStep(2, "Arduino tone 880 Hz");
  tone(pins::kAudio, 880);
  delay(stageMs);
  noTone(pins::kAudio);
  audioTestSilence();

  drawAudioTestStep(3, "LEDC kanal 0 880 Hz");
  ledcSetup(0, 880, 10);
  ledcAttachPin(pins::kAudio, 0);
  ledcWriteTone(0, 880);
  delay(stageMs);
  audioTestSilence();

  drawAudioTestStep(4, "LEDC kanal 6 1200 Hz");
  ledcSetup(6, 1200, 10);
  ledcAttachPin(pins::kAudio, 6);
  ledcWriteTone(6, 1200);
  delay(stageMs);
  audioTestSilence();

  drawAudioTestStep(5, "DAC kvadrat 700 Hz");
  startedAt = millis();
  while (millis() - startedAt < stageMs) {
    dacWrite(pins::kAudio, 230);
    delayMicroseconds(714);
    dacWrite(pins::kAudio, 25);
    delayMicroseconds(714);
  }
  dacWrite(pins::kAudio, 128);
  delay(900);

  drawAudioTestStep(6, "DAC stopnice 880 Hz");
  constexpr uint8_t waveform[] = {128, 166, 198, 220, 228, 220, 198, 166,
                                  128, 90,  58,  36,  28,  36,  58,  90};
  startedAt = millis();
  while (millis() - startedAt < stageMs) {
    for (uint8_t index = 0; index < sizeof(waveform); ++index) {
      dacWrite(pins::kAudio, waveform[index]);
      delayMicroseconds(71);
    }
  }
  dacWrite(pins::kAudio, 128);
  delay(900);

  Serial.println("[AUDIO_TEST] complete");
  showHome();
}

void runFocusedAudioTest() {
  alarmAudio.stop();
  drawAudioTestStep(2, "ARDUINO TONE 880 HZ");
  Serial.println("[AUDIO_TEST] focused Arduino tone active for 15 seconds");
  dacDisable(pins::kAudio);
  pinMode(pins::kAudio, OUTPUT);
  tone(pins::kAudio, 880);
  delay(15000);
  noTone(pins::kAudio);
  audioTestSilence();
  showHome();
}

uint16_t audioRead16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1]) << 8;
}

uint32_t audioRead32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         static_cast<uint32_t>(value[1]) << 8 |
         static_cast<uint32_t>(value[2]) << 16 |
         static_cast<uint32_t>(value[3]) << 24;
}

void inspectAudioFile(const char* path) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[AUDIO_FILE] path=%s open=fail\n", path);
    return;
  }
  uint8_t riff[12];
  if (file.read(riff, sizeof(riff)) != sizeof(riff) ||
      memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    Serial.printf("[AUDIO_FILE] path=%s bytes=%u wav=no\n", path,
                  static_cast<unsigned>(file.size()));
    file.close();
    return;
  }
  uint16_t format = 0;
  uint16_t channels = 0;
  uint32_t rate = 0;
  uint16_t bits = 0;
  uint32_t dataBytes = 0;
  while (file.available() >= 8) {
    uint8_t chunk[8];
    if (file.read(chunk, sizeof(chunk)) != sizeof(chunk)) {
      break;
    }
    const uint32_t size = audioRead32(chunk + 4);
    const uint32_t start = file.position();
    if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
      uint8_t details[16];
      if (file.read(details, sizeof(details)) != sizeof(details)) {
        break;
      }
      format = audioRead16(details);
      channels = audioRead16(details + 2);
      rate = audioRead32(details + 4);
      bits = audioRead16(details + 14);
    } else if (memcmp(chunk, "data", 4) == 0) {
      dataBytes = size;
    }
    const uint32_t next = start + size + (size & 1U);
    if (next > file.size() || !file.seek(next)) {
      break;
    }
  }
  Serial.printf(
      "[AUDIO_FILE] path=%s bytes=%u wav=yes format=%u channels=%u "
      "rate=%u bits=%u data=%u compatible=%s\n",
      path, static_cast<unsigned>(file.size()), static_cast<unsigned>(format),
      static_cast<unsigned>(channels), static_cast<unsigned>(rate),
      static_cast<unsigned>(bits), static_cast<unsigned>(dataBytes),
      format == 1 && channels == 1 && rate == 22050 && bits == 16 &&
              dataBytes > 0
          ? "yes"
          : "no");
  file.close();
}

void inspectAudioFiles() {
  Serial.println("[AUDIO_SCAN] starting path=/clock/audio");
  File directory = SD.open("/clock/audio", FILE_READ);
  if (!directory || !directory.isDirectory()) {
    Serial.println("[AUDIO_SCAN] directory unavailable");
    directory.close();
    return;
  }
  uint16_t count = 0;
  for (File entry = directory.openNextFile(); entry;
       entry = directory.openNextFile()) {
    if (!entry.isDirectory()) {
      char path[128];
      const char* name = entry.name();
      if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
      } else {
        snprintf(path, sizeof(path), "/clock/audio/%s", name);
      }
      entry.close();
      inspectAudioFile(path);
      ++count;
    } else {
      entry.close();
    }
  }
  directory.close();
  Serial.printf("[AUDIO_SCAN] complete files=%u\n",
                static_cast<unsigned>(count));
}

void runFrequencySweep() {
  alarmAudio.stop();
  constexpr uint16_t frequencies[] = {100, 200, 300, 400, 500, 700,
                                      880, 1200, 1600, 2200, 3000, 4000};
  display.fillScreen(0x0000);
  unicodeText.setFont(u8g2_font_helvB14_te);
  unicodeText.setFontMode(1);
  unicodeText.setForegroundColor(kWarmWhite);
  unicodeText.drawUTF8(20, 38, "FREKVENČNI TEST");
  ledcSetup(0, frequencies[0], 10);
  ledcAttachPin(pins::kAudio, 0);
  for (uint8_t index = 0;
       index < sizeof(frequencies) / sizeof(frequencies[0]); ++index) {
    display.fillRect(0, 55, 320, 185, 0x0000);
    char label[20];
    snprintf(label, sizeof(label), "%u Hz", frequencies[index]);
    unicodeText.setFont(u8g2_font_logisoso32_tf);
    unicodeText.setFontMode(1);
    unicodeText.setForegroundColor(0xEBAC);
    const int16_t width = unicodeText.getUTF8Width(label);
    unicodeText.drawUTF8((display.width() - width) / 2, 145, label);
    unicodeText.setFont(u8g2_font_6x12_te);
    unicodeText.setFontMode(1);
    unicodeText.setForegroundColor(0xAD55);
    unicodeText.drawUTF8(74, 205, "2 SEKUNDI ZVOKA");
    ledcWriteTone(0, frequencies[index]);
    Serial.printf("[AUDIO_SWEEP] frequency=%u\n",
                  static_cast<unsigned>(frequencies[index]));
    delay(2000);
    ledcWriteTone(0, 0);
    delay(750);
  }
  ledcDetachPin(pins::kAudio);
  digitalWrite(pins::kAudio, LOW);
  Serial.println("[AUDIO_SWEEP] complete");
  showHome();
}
}  // namespace

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(kSerialBaud);
  delay(500);
  Serial.println("\nMateja Clock - Phase 4 Messages");
  Serial.printf("[BOOT] free_heap=%u\n", ESP.getFreeHeap());

  brightness.begin();
  initializeTouch();
  displaySpi.begin(pins::kTftClock, pins::kTftMiso, pins::kTftMosi,
                   pins::kTftChipSelect);
  display.init(240, 320, SPI_MODE0);
  display.setRotation(1);
  display.invertDisplay(false);
  display.setTextWrap(false);
  unicodeText.begin(display);
  drawLoadingScreen();

  // Mount the SD card before Wi-Fi/time networking so the credentials can be
  // read from /clock/config/wifi.json.
  photoStartResult = photos.begin();
  sdIsMounted = photoStartResult != PhotoService::StartResult::kSdUnavailable;
  const bool sdMounted = sdIsMounted;
  if (sdIsMounted) {
    SD.mkdir("/emoji");
    SD.mkdir("/emoji/32");
    SD.mkdir("/emoji/48");
  }

  const wificonfig::Credentials& wifi = loadWifiCredentials(sdMounted);
  clockTime.begin(wifi.ssid, wifi.password);
  rtcLink.begin(Serial1);
  clockTime.setFallbackEpochProvider([]() -> std::int64_t {
    return rtcLink.rtcValid() ? rtcLink.rtcEpoch() : 0;
  });
  alarmService.begin(sdMounted);
  messageService.begin(sdMounted);
  otaService = new (std::nothrow) OtaService();
  if (otaService == nullptr) {
    Serial.println("[OTA] allocation failed; OTA disabled");
  } else {
    otaService->begin(sdMounted);
  }
  photos.setBottomGradient(true);

  const ntfyconfig::Config& storedNtfy = loadNtfyConfig(sdMounted);
  NtfyClient::Config ntfyConfig;
  ntfyConfig.baseUrl = storedNtfy.baseUrl;
  ntfyConfig.inboxTopic = storedNtfy.inboxTopic;
  ntfyConfig.ackTopic = storedNtfy.ackTopic;
  ntfyConfig.token = storedNtfy.accessToken;
  ntfyConfig.caCert = kMATEJA_NTFY_CA_CERT;  // public GTS Root R4 trust anchor
  if (storedNtfy.caCert[0] != '\0') {
    ntfyConfig.caCert = storedNtfy.caCert;
  }
  ntfyClient.begin(ntfyConfig, messageService);

  homeScreen.setPhotoStartResult(photoStartResult);
  homeScreen.showInitial();
  markPhotoChanged();
  photos.printStats();
  printCommands();
  if (otaService != nullptr) {
    otaService->markSetupHealthy(sdMounted && SD.exists(kWavPath));
  }
}

void updateMessagePopup() {
  if (screenMode != ScreenMode::Home) {
    messagePopup.dismiss();
    return;
  }
  messagePopup.update();
  if (messagePopup.active()) {
    return;
  }
  // A fresh unread message on the Home screen deserves a popup.
  const MessageStatus status = messageService.status();
  if (status.unreadCount == 0 || messageService.count() == 0) {
    return;
  }
  // The newest unread message is the first unread entry in index order
  // (indices are newest-first).
  std::size_t newestUnread = 0;
  bool found = false;
  for (std::size_t index = 0; index < messageService.count(); ++index) {
    if (!messageService.at(index).read) {
      newestUnread = index;
      found = true;
      break;
    }
  }
  if (!found) {
    return;
  }
  const MessageService::Summary& newest = messageService.at(newestUnread);
  if (!newest.read && std::strcmp(newest.id, lastPopupMessageId) != 0) {
    std::strncpy(lastPopupMessageId, newest.id,
                 sizeof(lastPopupMessageId) - 1);
    lastPopupMessageId[sizeof(lastPopupMessageId) - 1] = '\0';
    messagePopup.show(newestUnread);
    homeScreen.refresh();
  }
}

void loop() {
  if (wavPendingBytes != 0) {
    drawWavStatus();
    const WavUploader::Result result =
        wavUploader.receive(Serial, wavPendingBytes, kWavPath);
    wavPendingBytes = 0;
    drainSerial();
    Serial.printf("[WAV_UPLOAD] received=%u chunks=%u src_us=%u\n",
                  wavUploader.lastReceivedBytes(), wavUploader.chunksWritten(),
                  wavUploader.sourceMicros());
    printWavResult(result);
    showHome();
    return;
  }

  if (uploadPendingBytes != 0) {
    if (sdIsMounted) {
      // Auto-create parent directory
      char* lastSlash = std::strrchr(uploadPath, '/');
      if (lastSlash != nullptr && lastSlash != uploadPath) {
        *lastSlash = '\0';
        SD.mkdir(uploadPath);
        *lastSlash = '/';
      }
      File f = SD.open(uploadPath, FILE_WRITE);
      if (f) {
        uint8_t buf[512];
        uint32_t remaining = uploadPendingBytes;
        const uint32_t start = millis();
        while (remaining > 0) {
          const uint32_t count = min<uint32_t>(remaining, sizeof(buf));
          const int32_t read = Serial.readBytes(buf, static_cast<int>(count));
          if (read > 0) {
            f.write(buf, static_cast<size_t>(read));
            remaining -= static_cast<uint32_t>(read);
          } else if (millis() - start > 10000) {
            break;
          }
        }
        f.close();
        uint32_t duration = millis() - start;
        Serial.printf("[FILE] received %u bytes to %s in %u ms\n",
                      uploadPendingBytes, uploadPath, duration);
      } else {
        Serial.printf("[FILE] failed to open %s for writing\n", uploadPath);
      }
    } else {
      Serial.println("[FILE] sd not available");
    }
    uploadPendingBytes = 0;
    uploadPath[0] = '\0';
    drainSerial();
    return;
  }

  handleSerial();
  rtcLink.update();
  handlePhysicalInputs();
  handleTouch();
  // Hold-to-repeat for Alarm Settings time buttons.
  if (screenMode == ScreenMode::AlarmSettings) {
    const uint32_t now = millis();
    if (touchIsStable) {
      alarmSettings.checkRepeatStart(touchDownX, touchDownY, now);
      alarmSettings.handleRepeat(true, touchDownX, touchDownY, now);
    } else {
      alarmSettings.handleRepeat(false, 0, 0, now);
    }
  }
  brightness.update();
  alarmAudio.update();

  const bool minuteChanged = clockTime.update();
  // Keep the DS1302 coprocessor in sync while NTP is authoritative, so the
  // clock stays accurate offline too.
  if (clockTime.source() == timesource::Source::kNtp &&
      clockTime.snapshot().valid) {
    rtcLink.synchronize(clockTime.snapshot().epochSeconds);
  }
  updateMessagePopup();
  if (alarmService.update(clockTime.snapshot())) {
    showRinging();
  }
  OtaService::Conditions otaConditions;
  otaConditions.wifiConnected = clockTime.wifiConnected();
  otaConditions.timeValid = clockTime.snapshot().valid;
  otaConditions.alarmActive =
      alarmService.state() != alarmclock::AlarmState::Armed;
  otaConditions.alarmAudioPlaying = alarmAudio.isPlaying();
  otaConditions.rebootSafe =
      !otaConditions.alarmActive && !otaConditions.alarmAudioPlaying &&
      minutesUntilNextScheduledAlarm() > kOtaAlarmSafetyMinutes;
  const bool otaExclusive = otaService != nullptr &&
      otaService->needsNetworkExclusivity();
  ntfyClient.setPaused(otaExclusive);
  ntfyClient.update(clockTime.wifiConnected());
  if (otaService != nullptr) otaService->update(otaConditions);
  if (screenMode == ScreenMode::Ringing) {
    const TimeService::Snapshot& now = clockTime.snapshot();
    ringingScreen.refreshCurrentTime(now.valid, now.hour, now.minute);
  }
  // Persist volume to SD only after it has been stable for a while.
  if (volumePersistPending && millis() - volumeChangedAtMs >= kVolumeStablePersistMs) {
    volumePersistPending = false;
    alarmService.setVolume(lastVolume);
    Serial.printf("[VOLUME] persisted=%u\n", static_cast<unsigned>(lastVolume));
  }

  // Dismiss volume overlay after timeout.
  if (volumeOverlayVisible &&
      static_cast<uint32_t>(millis() - volumeOverlayStartMs) >= 1500) {
    dismissVolumeOverlay();
  }

  const uint32_t now = millis();
  if (screenMode == ScreenMode::Home) {
    if (photoStartResult == PhotoService::StartResult::kReady &&
        now - lastPhotoChangeAt >= kSlideshowIntervalMs) {
      markPhotoChanged();
      homeScreen.randomPhoto();
    } else if (minuteChanged) {
      homeScreen.refresh();
    } else if (messagePopup.active()) {
      messagePopup.draw();
    }
  }
  delay(2);
}
