#pragma once

#include <cstddef>

// Runtime Wi-Fi credentials stored on the SD card at /clock/config/wifi.json.
// The file is uploaded by tools/upload_wifi_config.py and is never committed.
namespace wificonfig {

constexpr char kConfigPath[] = "/clock/config/wifi.json";
constexpr std::size_t kMaxConfigBytes = 512;
constexpr std::size_t kMaxSsidBytes = 65;      // 64 chars + NUL
constexpr std::size_t kMaxPasswordBytes = 65;  // 64 chars + NUL

struct Credentials {
  char ssid[kMaxSsidBytes] = {0};
  char password[kMaxPasswordBytes] = {0};
  bool valid = false;
};

// Parses {"ssid":"...","password":"..."} (the password key is optional).
// Returns valid=false when the payload is not a JSON object, the ssid is
// missing or empty, a value overflows, or an unsupported escape is used.
Credentials Parse(const char* data, std::size_t length);

}  // namespace wificonfig
