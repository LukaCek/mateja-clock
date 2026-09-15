#include "TimeService.h"

#include <WiFi.h>
#include <time.h>

#include "MessageLogic.h"

namespace {

constexpr char kTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char kNtpPrimary[] = "pool.ntp.org";
constexpr char kNtpSecondary[] = "time.cloudflare.com";
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kReconnectIntervalMs = 60000;
constexpr time_t kMinimumValidEpoch = 1700000000;

}  // namespace

void TimeService::begin(const char* ssid, const char* password) {
  ssid_ = ssid;
  password_ = password;
  credentialsAvailable_ = ssid_ != nullptr && ssid_[0] != '\0';

  setenv("TZ", kTimezone, 1);
  tzset();

  if (!credentialsAvailable_) {
    Serial.println("[TIME] Wi-Fi credentials unavailable; staying offline");
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[TIME] Wi-Fi disconnected reason=%u\n",
                    static_cast<unsigned>(info.wifi_sta_disconnected.reason));
    }
  });
  startConnection();
}

void TimeService::startConnection() {
  const wl_status_t beginStatus = WiFi.begin(ssid_, password_);
  connectionStartedAt_ = millis();
  lastConnectionAttemptAt_ = connectionStartedAt_;
  connecting_ = true;
  Serial.printf("[TIME] Wi-Fi connection started status=%u ssid_len=%u psk_len=%u\n",
                static_cast<unsigned>(beginStatus), strlen(ssid_),
                password_ == nullptr ? 0U : strlen(password_));
}

bool TimeService::update() {
  const uint32_t nowMs = millis();
  if (credentialsAvailable_ && WiFi.status() == WL_CONNECTED) {
    connecting_ = false;
    if (!ntpConfigured_) {
      configTzTime(kTimezone, kNtpPrimary, kNtpSecondary);
      ntpConfigured_ = true;
      Serial.printf("[TIME] Wi-Fi connected rssi=%d; NTP configured\n",
                    WiFi.RSSI());
    }
  } else if (credentialsAvailable_) {
    if (connecting_ && nowMs - connectionStartedAt_ >= kConnectTimeoutMs) {
      connecting_ = false;
      Serial.printf("[TIME] Wi-Fi connection timed out status=%u; will retry\n",
                    static_cast<unsigned>(WiFi.status()));
    }
    if (!connecting_ && nowMs - lastConnectionAttemptAt_ >=
                            kReconnectIntervalMs) {
      startConnection();
    }
  }

  const time_t now = time(nullptr);
  if (now < kMinimumValidEpoch) {
    if (!snapshot_.valid) {
      return false;
    }
    snapshot_ = Snapshot{};
    displayedMinute_ = 0;
    return true;
  }

  struct tm localTime {};
  localtime_r(&now, &localTime);
  const time_t minute = now / 60;
  snapshot_.epochSeconds = now;
  if (snapshot_.valid && minute == displayedMinute_) {
    return false;
  }

  snapshot_.valid = true;
  snapshot_.year = localTime.tm_year + 1900;
  snapshot_.yearDay = localTime.tm_yday;
  snapshot_.hour = localTime.tm_hour;
  snapshot_.minute = localTime.tm_min;
  snapshot_.day = localTime.tm_mday;
  snapshot_.weekday = localTime.tm_wday;
  snapshot_.month = localTime.tm_mon;
  displayedMinute_ = minute;
  Serial.printf("[TIME] local=%02d:%02d day=%d weekday=%d month=%d\n",
                snapshot_.hour, snapshot_.minute, snapshot_.day,
                snapshot_.weekday, snapshot_.month);
  return true;
}

bool TimeService::wifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

std::int32_t TimeService::utcOffsetSeconds() const {
  if (!snapshot_.valid) {
    return 0;
  }
  const std::int64_t localFloor =
      messagelogic::daysFromCivil(snapshot_.year, snapshot_.month,
                                  snapshot_.day) *
          86400 +
      static_cast<std::int64_t>(snapshot_.hour) * 3600 +
      static_cast<std::int64_t>(snapshot_.minute) * 60;
  const std::int64_t utcFloor =
      (snapshot_.epochSeconds / 60) * 60;
  return static_cast<std::int32_t>(localFloor - utcFloor);
}

void TimeService::printStatus() const {
  Serial.printf("[TIME_STATUS] wifi=%s ntp=%s valid=%s time=%02d:%02d\n",
                wifiConnected() ? "connected" : "offline",
                ntpConfigured_ ? "configured" : "not_configured",
                snapshot_.valid ? "yes" : "no", snapshot_.hour,
                snapshot_.minute);
}
