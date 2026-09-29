#include "OtaService.h"

#include <SD.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr char kConfigPath[] = "/clock/config/ota.json";
constexpr std::size_t kMaxManifestBytes = 2048;
constexpr std::size_t kChunkBytes = 1024;
constexpr std::uint32_t kReadIdleTimeoutMs = 10000;
constexpr std::uint32_t kHealthDelayMs = 15000;
constexpr std::uint32_t kHealthLoops = 100;
constexpr unsigned kMaxRedirects = 5;

extern const uint8_t x509_crt_bundle_start[]
    asm("_binary_certs_x509_crt_bundle_bin_start");

bool extractRepository(const char* json, char* output, std::size_t capacity) {
  const char* key = std::strstr(json, "\"repository\"");
  if (key == nullptr) return false;
  const char* colon = std::strchr(key + 12, ':');
  if (colon == nullptr) return false;
  const char* start = colon + 1;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
  if (*start++ != '"') return false;
  const char* end = std::strchr(start, '"');
  if (end == nullptr) return false;
  const std::size_t length = static_cast<std::size_t>(end - start);
  if (length == 0 || length >= capacity) return false;
  const char* slash = static_cast<const char*>(std::memchr(start, '/', length));
  if (slash == nullptr || slash == start || slash == end - 1 ||
      std::memchr(slash + 1, '/', static_cast<std::size_t>(end - slash - 1)) != nullptr) {
    return false;
  }
  for (const char* cursor = start; cursor != end; ++cursor) {
    const char value = *cursor;
    if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '-' || value == '_' ||
          value == '.' || value == '/')) return false;
  }
  std::memcpy(output, start, length);
  output[length] = '\0';
  return true;
}

void bytesToHex(const unsigned char* input, std::size_t length, char* output) {
  static const char digits[] = "0123456789abcdef";
  for (std::size_t index = 0; index < length; ++index) {
    output[index * 2] = digits[input[index] >> 4];
    output[index * 2 + 1] = digits[input[index] & 0x0F];
  }
  output[length * 2] = '\0';
}

bool extractHost(const char* url, char* output, std::size_t capacity) {
  static const char prefix[] = "https://";
  if (url == nullptr || output == nullptr || capacity == 0 ||
      std::strncmp(url, prefix, sizeof(prefix) - 1) != 0) {
    return false;
  }
  const char* host = url + sizeof(prefix) - 1;
  const char* slash = std::strchr(host, '/');
  if (slash == nullptr || slash == host) return false;
  const std::size_t length = static_cast<std::size_t>(slash - host);
  if (length >= capacity) return false;
  std::memcpy(output, host, length);
  output[length] = '\0';
  return true;
}

}  // namespace

extern "C" bool verifyRollbackLater() { return true; }

void OtaService::begin(bool sdMounted) {
  configured_ = loadRepository(sdMounted);
  mbedtls_sha256_init(&sha_);

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t imageState;
  pendingVerify_ = running != nullptr &&
      esp_ota_get_state_partition(running, &imageState) == ESP_OK &&
      imageState == ESP_OTA_IMG_PENDING_VERIFY;
  if (pendingVerify_) {
    healthStartedMs_ = millis();
    Serial.println("[OTA] first boot pending validation");
  }
  Serial.printf("[OTA] version=%s configured=%s slots=%u\n",
                MATEJA_CLOCK_VERSION, configured_ ? "yes" : "no",
                static_cast<unsigned>(esp_ota_get_app_partition_count()));
}

void OtaService::markSetupHealthy(bool healthy) {
  setupHealthKnown_ = true;
  setupHealthy_ = healthy;
}

bool OtaService::loadRepository(bool sdMounted) {
  if (!sdMounted) return false;
  File file = SD.open(kConfigPath, FILE_READ);
  if (!file || file.size() == 0 || file.size() >= 256) {
    if (file) file.close();
    return false;
  }
  char json[256];
  const std::size_t read = file.readBytes(json, sizeof(json) - 1);
  file.close();
  json[read] = '\0';
  if (!extractRepository(json, repository_, sizeof(repository_))) return false;
  const int manifestLength = std::snprintf(
      manifestUrl_, sizeof(manifestUrl_),
      "https://github.com/%s/releases/latest/download/manifest.json", repository_);
  const int firmwareLength = std::snprintf(
      firmwareUrl_, sizeof(firmwareUrl_),
      "https://github.com/%s/releases/latest/download/mateja-clock.bin", repository_);
  return manifestLength > 0 && firmwareLength > 0 &&
      static_cast<std::size_t>(manifestLength) < sizeof(manifestUrl_) &&
      static_cast<std::size_t>(firmwareLength) < sizeof(firmwareUrl_);
}

bool OtaService::requestCheck() {
  if (busy() || rebootPending()) return false;
  installRequested_ = false;
  requestPending_ = true;
  return true;
}

bool OtaService::requestUpdate() {
  if (busy() || rebootPending()) return false;
  installRequested_ = true;
  requestPending_ = true;
  return true;
}

bool OtaService::busy() const {
  return state_ == otalogic::OtaState::kChecking ||
         state_ == otalogic::OtaState::kDownloading;
}

bool OtaService::rebootPending() const {
  return state_ == otalogic::OtaState::kReadyToReboot ||
         state_ == otalogic::OtaState::kRebootDeferred;
}

void OtaService::updateFirstBootHealth() {
  if (!pendingVerify_) return;
  ++healthyLoops_;
  if (setupHealthKnown_ && !setupHealthy_) {
    Serial.println("[OTA] local health failed; rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }
#ifdef MATEJA_OTA_FORCE_UNHEALTHY
  if (millis() - healthStartedMs_ >= 5000) {
    Serial.println("[OTA] forced unhealthy acceptance image; restarting");
    esp_restart();
  }
#endif
  if (setupHealthKnown_ && setupHealthy_ && healthyLoops_ >= kHealthLoops &&
      millis() - healthStartedMs_ >= kHealthDelayMs) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pendingVerify_ = false;
      Serial.println("[OTA] first boot validated");
    }
  }
}

void OtaService::update(const Conditions& conditions) {
  updateFirstBootHealth();

  if (conditions.alarmActive && busy()) {
    abortDownload("alarm started", true);
    return;
  }
  if (requestPending_) {
    requestPending_ = false;
    if (!configured_) { fail("repository not configured"); return; }
    if (!conditions.wifiConnected || !conditions.timeValid) {
      fail("Wi-Fi/time unavailable"); return;
    }
    if (conditions.alarmActive || conditions.alarmAudioPlaying ||
        !conditions.rebootSafe) {
      fail("alarm safety window active"); return;
    }
    checkManifest(installRequested_);
    return;
  }
  if (state_ == otalogic::OtaState::kDownloading) {
    pumpFirmwareDownload(conditions);
    return;
  }
  if (state_ == otalogic::OtaState::kChecking) {
    pumpManifest(conditions);
    return;
  }
  if (rebootPending()) {
    if (!conditions.rebootSafe || conditions.alarmActive ||
        conditions.alarmAudioPlaying) {
      state_ = otalogic::OtaState::kRebootDeferred;
      return;
    }
    Serial.println("[OTA] rebooting into verified image");
    delay(100);
    esp_restart();
  }
}

bool OtaService::checkManifest(bool installRequested) {
  state_ = otalogic::OtaState::kChecking;
  if (!openUrl(manifestUrl_, 0, false)) return false;
  const int contentLength = http_.getSize();
  if (contentLength <= 0 || contentLength > static_cast<int>(kMaxManifestBytes)) {
    closeHttp(); fail("manifest size invalid"); return false;
  }
  manifestExpected_ = static_cast<std::uint32_t>(contentLength);
  manifestReceived_ = 0;
  lastDataMs_ = millis();
  installRequested_ = installRequested;
  return true;
}

void OtaService::pumpManifest(const Conditions& conditions) {
  if (!conditions.wifiConnected) {
    abortDownload("Wi-Fi lost", false); return;
  }
  const int available = stream_ != nullptr ? stream_->available() : 0;
  if (available <= 0) {
    if ((stream_ == nullptr || !stream_->connected()) &&
        manifestReceived_ < manifestExpected_) {
      abortDownload("manifest connection closed", false);
    } else if (millis() - lastDataMs_ >= kReadIdleTimeoutMs) {
      abortDownload("manifest timeout", false);
    }
    return;
  }
  const std::size_t remaining = manifestExpected_ - manifestReceived_;
  const std::size_t amount = min<std::size_t>(
      min<std::size_t>(static_cast<std::size_t>(available), kChunkBytes), remaining);
  const int read = stream_->read(reinterpret_cast<uint8_t*>(
      manifestJson_ + manifestReceived_), amount);
  if (read <= 0) return;
  manifestReceived_ += static_cast<std::uint32_t>(read);
  lastDataMs_ = millis();
  if (manifestReceived_ == manifestExpected_) processManifest();
}

void OtaService::processManifest() {
  closeHttp();
  manifestJson_[manifestReceived_] = '\0';
  if (!otalogic::parseManifest(manifestJson_, manifest_)) {
    fail("manifest malformed"); return;
  }
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) { fail("inactive OTA slot unavailable"); return; }
  otalogic::SemVer current;
  if (!otalogic::parseSemVer(MATEJA_CLOCK_VERSION, current)) {
    fail("compiled version invalid"); return;
  }
  const otalogic::UpdateDecision decision =
      otalogic::decideUpdate(manifest_, current, target->size);
  std::snprintf(availableVersion_, sizeof(availableVersion_), "%s", manifest_.version);
  if (decision == otalogic::UpdateDecision::kDowngrade) {
    fail("downgrade rejected"); return;
  }
  if (decision == otalogic::UpdateDecision::kInvalidManifest) {
    fail("manifest rejected"); return;
  }
  if (decision == otalogic::UpdateDecision::kUpToDate) {
    state_ = otalogic::OtaState::kIdle;
    Serial.println("[OTA] already up to date");
    return;
  }
  Serial.printf("[OTA] update available version=%s bytes=%u\n",
                manifest_.version, static_cast<unsigned>(manifest_.size));
  state_ = otalogic::OtaState::kIdle;
  if (installRequested_) beginFirmwareDownload();
}

bool OtaService::beginFirmwareDownload() {
  target_ = esp_ota_get_next_update_partition(nullptr);
  if (target_ == nullptr || manifest_.size > target_->size) {
    fail("inactive slot too small"); return false;
  }
  if (!openUrl(firmwareUrl_, manifest_.size, true)) return false;
  if (esp_ota_begin(target_, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle_) != ESP_OK) {
    closeHttp(); fail("esp_ota_begin failed"); return false;
  }
  otaOpen_ = true;
  received_ = 0;
  lastDataMs_ = millis();
  mbedtls_sha256_free(&sha_);
  mbedtls_sha256_init(&sha_);
  if (mbedtls_sha256_starts_ret(&sha_, 0) != 0) {
    abortDownload("SHA-256 init failed", false); return false;
  }
  shaOpen_ = true;
  state_ = otalogic::OtaState::kDownloading;
  Serial.printf("[OTA] downloading target=%s size=%u\n", target_->label,
                static_cast<unsigned>(manifest_.size));
  return true;
}

void OtaService::pumpFirmwareDownload(const Conditions& conditions) {
  if (!conditions.wifiConnected) {
    abortDownload("Wi-Fi lost", false); return;
  }
  const int available = stream_ != nullptr ? stream_->available() : 0;
  if (available <= 0) {
    if ((stream_ == nullptr || !stream_->connected()) && received_ < manifest_.size) {
      abortDownload("connection closed", false);
    } else if (millis() - lastDataMs_ >= kReadIdleTimeoutMs) {
      abortDownload("download timeout", false);
    }
    return;
  }
  uint8_t buffer[kChunkBytes];
  const std::size_t remaining = manifest_.size - received_;
  const std::size_t amount = min<std::size_t>(
      min<std::size_t>(static_cast<std::size_t>(available), sizeof(buffer)), remaining);
  const int read = stream_->read(buffer, amount);
  if (read <= 0) return;
  lastDataMs_ = millis();
  if (esp_ota_write(otaHandle_, buffer, static_cast<std::size_t>(read)) != ESP_OK ||
      mbedtls_sha256_update_ret(&sha_, buffer, static_cast<std::size_t>(read)) != 0) {
    abortDownload("flash/hash write failed", false); return;
  }
  received_ += static_cast<std::uint32_t>(read);
  if (received_ == manifest_.size) finishDownload();
}

void OtaService::finishDownload() {
  unsigned char digest[32];
  if (!shaOpen_ || mbedtls_sha256_finish_ret(&sha_, digest) != 0) {
    abortDownload("SHA-256 finish failed", false); return;
  }
  shaOpen_ = false;
  mbedtls_sha256_free(&sha_);
  char digestHex[65];
  bytesToHex(digest, sizeof(digest), digestHex);
  if (std::strcmp(digestHex, manifest_.sha256) != 0) {
    abortDownload("SHA-256 mismatch", false); return;
  }
  closeHttp();
  const esp_err_t endResult = esp_ota_end(otaHandle_);
  otaOpen_ = false;
  if (endResult != ESP_OK) { fail("image validation failed"); return; }
  unsigned char partitionDigest[32];
  if (esp_partition_get_sha256(target_, partitionDigest) != ESP_OK) {
    fail("partition image validation failed"); return;
  }
  if (esp_ota_set_boot_partition(target_) != ESP_OK) {
    fail("boot partition selection failed"); return;
  }
  state_ = otalogic::OtaState::kReadyToReboot;
  Serial.printf("[OTA] verified bytes=%u sha256=%s target=%s\n",
                static_cast<unsigned>(received_), digestHex, target_->label);
}

bool OtaService::allowedUrl(const char* url) const {
  static const char prefix[] = "https://";
  if (std::strncmp(url, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* host = url + sizeof(prefix) - 1;
  const char* slash = std::strchr(host, '/');
  if (slash == nullptr || slash == host) return false;
  const std::size_t length = static_cast<std::size_t>(slash - host);
  return (length == std::strlen("github.com") &&
          std::strncmp(host, "github.com", length) == 0) ||
         (length == std::strlen("release-assets.githubusercontent.com") &&
          std::strncmp(host, "release-assets.githubusercontent.com", length) == 0);
}

bool OtaService::openUrl(const char* initialUrl, std::uint32_t expectedSize,
                         bool requireLength) {
  char url[512];
  std::snprintf(url, sizeof(url), "%s", initialUrl);
  const char* headerKeys[] = {"Location"};
  for (unsigned redirect = 0; redirect <= kMaxRedirects; ++redirect) {
    if (!allowedUrl(url)) { fail("redirect host rejected"); return false; }
    char host[64];
    if (!extractHost(url, host, sizeof(host))) {
      fail("HTTPS host invalid"); return false;
    }
    Serial.printf("[OTA_HTTP] request host=%s hop=%u\n", host, redirect);
    tls_.stop();
    tls_.setCACertBundle(x509_crt_bundle_start);
    tls_.setHandshakeTimeout(3);
    tls_.setTimeout(3);
    http_.setReuse(false);
    http_.setConnectTimeout(3000);
    http_.setTimeout(3000);
    http_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http_.collectHeaders(headerKeys, 1);
    if (!http_.begin(tls_, url)) { fail("HTTPS begin failed"); return false; }
    const int status = http_.GET();
    if (status == HTTP_CODE_MOVED_PERMANENTLY || status == HTTP_CODE_FOUND ||
        status == HTTP_CODE_SEE_OTHER || status == HTTP_CODE_TEMPORARY_REDIRECT ||
        status == HTTP_CODE_PERMANENT_REDIRECT) {
      const String location = http_.header("Location");
      http_.end();
      if (location.length() == 0 || location.length() >= sizeof(url)) {
        closeHttp(); fail("redirect invalid"); return false;
      }
      if (location[0] == '/') {
        std::snprintf(url, sizeof(url), "https://github.com%s", location.c_str());
      } else {
        std::snprintf(url, sizeof(url), "%s", location.c_str());
      }
      if (!allowedUrl(url) || !extractHost(url, host, sizeof(host))) {
        closeHttp(); fail("redirect host rejected"); return false;
      }
      Serial.printf("[OTA_HTTP] redirect host=%s\n", host);
      continue;
    }
    if (status != HTTP_CODE_OK) { closeHttp(); fail("HTTPS status failed"); return false; }
    const int length = http_.getSize();
    if (requireLength && (length < 0 || static_cast<std::uint32_t>(length) != expectedSize)) {
      closeHttp(); fail("content length mismatch"); return false;
    }
    stream_ = http_.getStreamPtr();
    return stream_ != nullptr;
  }
  closeHttp();
  fail("too many redirects");
  return false;
}

void OtaService::abortDownload(const char* reason, bool cancelled) {
  closeHttp();
  if (otaOpen_) {
    esp_ota_abort(otaHandle_);
    otaOpen_ = false;
  }
  if (shaOpen_) {
    mbedtls_sha256_free(&sha_);
    shaOpen_ = false;
  }
  std::snprintf(lastError_, sizeof(lastError_), "%s", reason);
  state_ = cancelled ? otalogic::OtaState::kCancelled : otalogic::OtaState::kFailed;
  Serial.printf("[OTA] %s reason=%s\n", cancelled ? "cancelled" : "failed", reason);
}

void OtaService::fail(const char* reason) {
  std::snprintf(lastError_, sizeof(lastError_), "%s", reason);
  state_ = otalogic::OtaState::kFailed;
  Serial.printf("[OTA] failed reason=%s\n", reason);
}

void OtaService::closeHttp() {
  stream_ = nullptr;
  http_.end();
  tls_.stop();
}

const char* OtaService::stateName(otalogic::OtaState state) {
  switch (state) {
    case otalogic::OtaState::kIdle: return "idle";
    case otalogic::OtaState::kChecking: return "checking";
    case otalogic::OtaState::kDownloading: return "downloading";
    case otalogic::OtaState::kReadyToReboot: return "ready_to_reboot";
    case otalogic::OtaState::kRebootDeferred: return "reboot_deferred";
    case otalogic::OtaState::kCancelled: return "cancelled";
    case otalogic::OtaState::kFailed: return "failed";
  }
  return "unknown";
}

const char* OtaService::imageStateName(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    case ESP_OTA_IMG_UNDEFINED: return "undefined";
  }
  return "undefined";
}

void OtaService::printStatus() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t imageState = ESP_OTA_IMG_UNDEFINED;
  if (running == nullptr ||
      esp_ota_get_state_partition(running, &imageState) != ESP_OK) {
    imageState = ESP_OTA_IMG_UNDEFINED;
  }
  Serial.printf(
      "[OTA_STATUS] version=%s state=%s configured=%s repository=%s "
      "running=%s boot=%s target=%s image_state=%s slots=%u slot_bytes=%u received=%u "
      "available=%s reboot_pending=%s pending_verify=%s error=%s\n",
      MATEJA_CLOCK_VERSION, stateName(state_), configured_ ? "yes" : "no",
      configured_ ? repository_ : "(none)", running ? running->label : "none",
      boot ? boot->label : "none", next ? next->label : "none",
      imageStateName(imageState),
      static_cast<unsigned>(esp_ota_get_app_partition_count()),
      static_cast<unsigned>(next ? next->size : 0),
      static_cast<unsigned>(received_),
      availableVersion_[0] ? availableVersion_ : "none",
      rebootPending() ? "yes" : "no", pendingVerify_ ? "yes" : "no",
      lastError_[0] ? lastError_ : "none");
}
