#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "OtaLogic.h"

class OtaService {
 public:
  struct Conditions {
    bool wifiConnected = false;
    bool timeValid = false;
    bool alarmActive = false;
    bool alarmAudioPlaying = false;
    bool rebootSafe = false;
  };

  void begin(bool sdMounted);
  void markSetupHealthy(bool healthy);
  void update(const Conditions& conditions);

  bool requestCheck();
  bool requestUpdate();
  void printStatus() const;

  otalogic::OtaState state() const { return state_; }
  bool busy() const;
  bool needsNetworkExclusivity() const { return requestPending_ || busy(); }
  bool rebootPending() const;

 private:
  bool loadRepository(bool sdMounted);
  bool checkManifest(bool installRequested);
  void pumpManifest(const Conditions& conditions);
  void processManifest();
  bool beginFirmwareDownload();
  void pumpFirmwareDownload(const Conditions& conditions);
  bool openUrl(const char* initialUrl, std::uint32_t expectedSize,
               bool requireLength);
  bool allowedUrl(const char* url) const;
  void finishDownload();
  void abortDownload(const char* reason, bool cancelled);
  void fail(const char* reason);
  void closeHttp();
  void updateFirstBootHealth();
  static const char* stateName(otalogic::OtaState state);
  static const char* imageStateName(esp_ota_img_states_t state);

  char repository_[96] = {0};
  char manifestUrl_[256] = {0};
  char firmwareUrl_[256] = {0};
  bool configured_ = false;
  bool installRequested_ = false;
  bool requestPending_ = false;
  char manifestJson_[2049] = {0};
  std::uint32_t manifestExpected_ = 0;
  std::uint32_t manifestReceived_ = 0;

  otalogic::OtaState state_ = otalogic::OtaState::kIdle;
  otalogic::Manifest manifest_;
  char availableVersion_[otalogic::kMaxVersionBytes] = {0};
  char lastError_[128] = {0};

  WiFiClientSecure tls_;
  HTTPClient http_;
  WiFiClient* stream_ = nullptr;
  const esp_partition_t* target_ = nullptr;
  esp_ota_handle_t otaHandle_ = 0;
  bool otaOpen_ = false;
  std::uint32_t received_ = 0;
  std::uint32_t lastDataMs_ = 0;
  mbedtls_sha256_context sha_;
  bool shaOpen_ = false;

  bool pendingVerify_ = false;
  bool setupHealthKnown_ = false;
  bool setupHealthy_ = false;
  std::uint32_t healthStartedMs_ = 0;
  std::uint32_t healthyLoops_ = 0;
};
