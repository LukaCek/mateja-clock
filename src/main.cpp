#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "AlarmAudio.h"
#include "AlarmLogic.h"
#include "AlarmScreens.h"
#include "AlarmService.h"
#include "BrightnessService.h"
#include "HomeScreen.h"
#include "MessageScreens.h"
#include "MessageService.h"
#include "NtfyClient.h"
#include "PhotoService.h"
#include "RtcLinkService.h"
#include "StatusProviders.h"
#include "TimeService.h"
#include "WavUploader.h"
#include "hardware_pins.h"
#include "ntfy_ca_cert.h"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#define MATEJA_WIFI_SSID ""
#define MATEJA_WIFI_PASSWORD ""
#endif

#if __has_include("ntfy_credentials.h")
#include "ntfy_credentials.h"
#else
#define MATEJA_NTFY_BASE_URL ""
#define MATEJA_NTFY_INBOX_TOPIC ""
#define MATEJA_NTFY_ACK_TOPIC ""
#define MATEJA_NTFY_ACCESS_TOKEN ""
#endif

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
AlarmService alarmService;
AlarmAudio alarmAudio;
RtcLinkService rtcLink;
WavUploader wavUploader;

constexpr char kWavPath[] = "/clock/audio/alarm.wav";
uint32_t wavPendingBytes = 0;
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
  screenX = constrain(mappedX, 0, 319);
  screenY = constrain(mappedY, 0, 239);
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
      "!msg [list|unread|read INDEX|ack|inject <text>], !ntfy, !rtc [sync], "
      "!c3, !c3 ping");
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
    Serial.println(
        "[MSGRO_CMD] usage: !msg, !msg list, !msg unread, !msg read INDEX, "
        "!msg ack, !msg inject <text>");
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
  display.setRotation(3);
  display.invertDisplay(false);
  display.setTextWrap(false);
  unicodeText.begin(display);
  drawLoadingScreen();

  clockTime.begin(MATEJA_WIFI_SSID, MATEJA_WIFI_PASSWORD);
  rtcLink.begin(Serial1);
  clockTime.setFallbackEpochProvider([]() -> std::int64_t {
    return rtcLink.rtcValid() ? rtcLink.rtcEpoch() : 0;
  });
  photoStartResult = photos.begin();
  sdIsMounted = photoStartResult != PhotoService::StartResult::kSdUnavailable;
  const bool sdMounted = sdIsMounted;
  alarmService.begin(sdMounted);
  messageService.begin(sdMounted);
  photos.setBottomGradient(true);

  NtfyClient::Config ntfyConfig;
  ntfyConfig.baseUrl = MATEJA_NTFY_BASE_URL;
  ntfyConfig.inboxTopic = MATEJA_NTFY_INBOX_TOPIC;
  ntfyConfig.ackTopic =
      MATEJA_NTFY_ACK_TOPIC[0] != '\0' ? MATEJA_NTFY_ACK_TOPIC
                                       : MATEJA_NTFY_INBOX_TOPIC;
  ntfyConfig.token = MATEJA_NTFY_ACCESS_TOKEN;
  ntfyConfig.caCert = kMATEJA_NTFY_CA_CERT;  // public GTS Root R4 trust anchor
#ifdef MATEJA_NTFY_CA_CERT
  if (MATEJA_NTFY_CA_CERT != nullptr && MATEJA_NTFY_CA_CERT[0] != '\0') {
    ntfyConfig.caCert = MATEJA_NTFY_CA_CERT;  // custom CA overrides the root
  }
#endif
  ntfyClient.begin(ntfyConfig, messageService);

  homeScreen.setPhotoStartResult(photoStartResult);
  homeScreen.showInitial();
  markPhotoChanged();
  photos.printStats();
  printCommands();
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

  handleSerial();
  rtcLink.update();
  handlePhysicalInputs();
  handleTouch();
  brightness.update();
  alarmAudio.update();

  const bool minuteChanged = clockTime.update();
  ntfyClient.update(clockTime.wifiConnected());
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
  if (screenMode == ScreenMode::Ringing) {
    const TimeService::Snapshot& now = clockTime.snapshot();
    ringingScreen.refreshCurrentTime(now.valid, now.hour, now.minute);
  }
  // Persist volume to SD only after it has been stable for a while.
  if (volumePersistPending && millis() - volumeChangedAtMs >= kVolumeStablePersistMs) {
    volumePersistPending = false;
    alarmService.setVolume(lastVolume);
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
