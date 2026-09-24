#pragma once

#include <cstddef>

// Runtime ntfy configuration stored on the SD card. The limits keep values
// within NtfyClient's fixed host and HTTP request buffers.
namespace ntfyconfig {

constexpr char kConfigPath[] = "/clock/config/ntfy.json";
constexpr std::size_t kMaxConfigBytes = 4096;
constexpr std::size_t kMaxBaseUrlBytes = 128;
constexpr std::size_t kMaxTopicBytes = 65;
constexpr std::size_t kMaxAccessTokenBytes = 129;
constexpr std::size_t kMaxCaCertBytes = 2049;

struct Config {
  char baseUrl[kMaxBaseUrlBytes] = {0};
  char inboxTopic[kMaxTopicBytes] = {0};
  char ackTopic[kMaxTopicBytes] = {0};
  char accessToken[kMaxAccessTokenBytes] = {0};
  char caCert[kMaxCaCertBytes] = {0};
  bool valid = false;
};

// Parses the complete JSON document. base_url must be an HTTPS URL and
// inbox_topic must not be empty. An empty ack_topic is replaced by inbox_topic.
Config Parse(const char* data, std::size_t length);

}  // namespace ntfyconfig
