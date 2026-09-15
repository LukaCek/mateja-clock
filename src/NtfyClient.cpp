#include "NtfyClient.h"

#include <WiFi.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr uint16_t kDefaultHttpsPort = 443;
constexpr uint16_t kConnectTimeoutMs = 3000;
constexpr uint16_t kStreamReadTimeoutMs = 2000;
constexpr uint16_t kAckReadTimeoutMs = 5000;
constexpr uint32_t kStreamIdleDeadlineMs = 120000;  // ntfy keepalive ~45 s

constexpr char kUserAgent[] = "mateja-clock/1.0";

// Exponential backoff ladder, capped at 30 s. A successful operation resets
// the caller's ladder to 0.
uint32_t nextBackoff(uint32_t current) {
  if (current == 0) {
    return 2000;
  }
  if (current >= 30000) {
    return 30000;
  }
  return current * 2 > 30000 ? 30000 : current * 2;
}

// Splits "https://host[:port]" (optionally with a path prefix) into host and
// port. Returns false on malformed input.
bool splitBaseUrl(const char* baseUrl, char* host, std::size_t hostCapacity,
                  uint16_t& port) {
  if (baseUrl == nullptr || host == nullptr || hostCapacity == 0) {
    return false;
  }
  const char* scheme = std::strstr(baseUrl, "://");
  const char* authority = scheme != nullptr ? scheme + 3 : baseUrl;
  const char* path = std::strchr(authority, '/');
  const std::size_t authorityLength =
      path != nullptr ? static_cast<std::size_t>(path - authority)
                      : std::strlen(authority);
  if (authorityLength == 0 || authorityLength + 1 > hostCapacity) {
    return false;
  }
  std::memcpy(host, authority, authorityLength);
  host[authorityLength] = '\0';
  char* colon = std::strrchr(host, ':');
  if (colon != nullptr) {
    *colon = '\0';
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(colon + 1, &end, 10);
    if (end == nullptr || *end != '\0' || parsed == 0 || parsed > 65535UL) {
      return false;
    }
    port = static_cast<uint16_t>(parsed);
  } else {
    port = kDefaultHttpsPort;
  }
  return host[0] != '\0';
}

// Header values are case-insensitive per HTTP; a token scan is enough here.
bool containsWord(const char* text, const char* word) {
  const std::size_t wordLength = std::strlen(word);
  auto asciiLower = [](char value) {
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
  };
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    std::size_t index = 0;
    for (; index < wordLength && cursor[index] != '\0' &&
           asciiLower(cursor[index]) == asciiLower(word[index]);
         ++index) {
    }
    if (index != wordLength) {
      continue;
    }
    const char before = cursor == text ? ' ' : cursor[-1];
    const char after = cursor[wordLength];
    const bool boundary =
        (before < 'a' || before > 'z') && (before < 'A' || before > 'Z') &&
        after != '-' && after != '_';
    if (boundary) {
      return true;
    }
  }
  return false;
}

}  // namespace

NtfyClient::NtfyClient() : store_(nullptr) {}

void NtfyClient::begin(const Config& config, MessageService& store) {
  config_ = config;
  store_ = &store;
  reconnectBackoffMs_ = 0;
  reconnectAtMs_ = 0;
  ackPhase_ = AckPhase::kIdle;
  ackRequested_ = false;
  ackBackoffMs_ = 0;
  ackAtMs_ = 0;

  // Re-queue any acks lost by a prior reboot (message was marked read on
  // screen but the device restarted before the ack was POSTed).
  if (store_->firstPendingAckIndex() >= 0) {
    ackRequested_ = true;
    Serial.println("[NTFY] queued ack for pre-reboot read message");
  }

  Serial.println("[NTFY] configured");
}

void NtfyClient::update(bool wifiConnected) {
  if (config_.baseUrl == nullptr || config_.baseUrl[0] == '\0') {
    return;  // not configured; stays inert
  }

  if (!wifiConnected) {
    if (streamActive()) {
      stream_.stop();
      Serial.println("[NTFY] wifi lost; stream stopped");
    }
    streamState_ = StreamState::kIdle;
    lastSyncFailed_ = true;
    return;
  }

  pumpAck();

  if (streamState_ == StreamState::kIdle) {
    if (static_cast<int32_t>(millis() - reconnectAtMs_) >= 0) {
      startStreamConnect();
    }
    return;
  }
  pumpStream();
}

void NtfyClient::reconnectNow() {
  if (config_.baseUrl == nullptr || config_.baseUrl[0] == '\0') {
    return;
  }
  stream_.stop();
  streamState_ = StreamState::kIdle;
  reconnectBackoffMs_ = 0;
  reconnectAtMs_ = millis() + 500;  // allow in-flight data to settle
}

void NtfyClient::requestAck() {
  ackRequested_ = true;
}

const char* NtfyClient::stateName() const {
  switch (streamState_) {
    case StreamState::kIdle:
      return "idle";
    case StreamState::kConnecting:
      return "connecting";
    case StreamState::kAwaitHeader:
      return "awaiting headers";
    case StreamState::kChunkSize:
      return "chunk size";
    case StreamState::kChunkData:
      return "chunk data";
    case StreamState::kChunkCrLf:
      return "chunk crlf";
    case StreamState::kFinished:
      return "finished";
  }
  return "unknown";
}

void NtfyClient::printStatus() const {
  char host[128] = {0};
  uint16_t port = 0;
  if (!splitBaseUrl(config_.baseUrl, host, sizeof(host), port)) {
    std::strcpy(host, "unset");
  }
  Serial.printf(
      "[NTFY] topic=%s host=%s state=%s ack_phase=%u unread=%u "
      "ack_sent=%u ack_failed=%u\n",
      config_.inboxTopic != nullptr ? config_.inboxTopic : "", host,
      stateName(), static_cast<unsigned>(ackPhase_),
      store_ != nullptr ? store_->status().unreadCount : 0,
      static_cast<unsigned>(ackSentCount_),
      static_cast<unsigned>(ackFailCount_));
}

void NtfyClient::scheduleReconnect(uint32_t delayMs) {
  streamState_ = StreamState::kIdle;
  reconnectBackoffMs_ = delayMs;
  reconnectAtMs_ = millis() + delayMs;
}

void NtfyClient::startStreamConnect() {
  if (config_.baseUrl == nullptr || config_.baseUrl[0] == '\0' ||
      config_.inboxTopic == nullptr || config_.inboxTopic[0] == '\0') {
    scheduleReconnect(30000);  // misconfiguration: do not hammer
    return;
  }

  char host[128];
  uint16_t port = 0;
  if (!splitBaseUrl(config_.baseUrl, host, sizeof(host), port)) {
    Serial.println("[NTFY] invalid base_url (splitBaseUrl)");
    scheduleReconnect(30000);
    return;
  }

  stream_ = WiFiClientSecure();
  if (config_.caCert == nullptr || config_.caCert[0] == '\0') {
    Serial.println("[NTFY] no CA cert configured; stream requires TLS");
    streamState_ = StreamState::kIdle;
    scheduleReconnect(30000);
    return;
  }
  stream_.setCACert(config_.caCert);
  stream_.setHandshakeTimeout(kConnectTimeoutMs);
  stream_.setTimeout(kStreamReadTimeoutMs);

  // Redacted URL: host + topic only; token and full query are never logged.
  Serial.printf("[NTFY] connecting host=%s port=%u topic=%s\n", host, port,
                config_.inboxTopic);
  streamState_ = StreamState::kConnecting;
  const bool connected = stream_.connect(host, port);
  if (!connected) {
    Serial.println("[NTFY] stream connect failed");
    stream_.stop();
    lastSyncFailed_ = true;
    scheduleReconnect(nextBackoff(reconnectBackoffMs_));
    return;
  }

  // Build the JSON-stream GET request (since=latest, or since=<lastId>).
  char lastId[messagelogic::kMaxIdBytes];
  store_->lastProcessedId(lastId, sizeof(lastId));
  char sinceBuffer[messagelogic::kMaxIdBytes + 8];
  const bool haveSince =
      messagelogic::makeSinceParam(lastId, sinceBuffer, sizeof(sinceBuffer));
  const char* since = haveSince ? sinceBuffer : "since=latest";

  char request[768];
  int written = std::snprintf(
      request, sizeof(request),
      "GET /%s/json?%s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Connection: keep-alive\r\n"
      "User-Agent: %s\r\n"
      "Accept: application/json\r\n",
      config_.inboxTopic, since, host, kUserAgent);
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(request)) {
    Serial.println("[NTFY] request build failed");
    stream_.stop();
    lastSyncFailed_ = true;
    scheduleReconnect(nextBackoff(reconnectBackoffMs_));
    return;
  }
  if (config_.token != nullptr && config_.token[0] != '\0') {
    const int tokenHeader =
        std::snprintf(request + written, sizeof(request) - written,
                      "Authorization: Bearer %s\r\n", config_.token);
    if (tokenHeader <= 0 ||
        static_cast<std::size_t>(tokenHeader) >=
            sizeof(request) - static_cast<std::size_t>(written)) {
      Serial.println("[NTFY] request build failed (token)");
      stream_.stop();
      lastSyncFailed_ = true;
      scheduleReconnect(nextBackoff(reconnectBackoffMs_));
      return;
    }
    written += tokenHeader;
  }
  const int terminator =
      std::snprintf(request + written, sizeof(request) - written, "\r\n");
  if (terminator <= 0 ||
      static_cast<std::size_t>(terminator) >=
          sizeof(request) - static_cast<std::size_t>(written)) {
    stream_.stop();
    lastSyncFailed_ = true;
    scheduleReconnect(nextBackoff(reconnectBackoffMs_));
    return;
  }
  written += terminator;

  stream_.write(reinterpret_cast<const uint8_t*>(request),
                static_cast<std::size_t>(written));
  stream_.flush();

  recordLength_ = 0;
  recordOversize_ = false;
  chunked_ = false;
  statusOk_ = false;
  streamLastDataMs_ = millis();
  streamState_ = StreamState::kAwaitHeader;
  reconnectBackoffMs_ = 0;
  lastSyncFailed_ = false;
  Serial.printf("[NTFY] stream request sent since=%s\n", since);
}

void NtfyClient::pumpStream() {
  if (streamState_ == StreamState::kConnecting) {
    return;
  }
  if (!streamActive()) {
    return;
  }
  if (!stream_.connected()) {
    Serial.println("[NTFY] stream closed by server");
    stream_.stop();
    lastSyncFailed_ = true;
    scheduleReconnect(nextBackoff(reconnectBackoffMs_));
    return;
  }
  if (static_cast<int32_t>(millis() - streamLastDataMs_) >
      static_cast<int32_t>(kStreamIdleDeadlineMs)) {
    Serial.println("[NTFY] stream idle watchdog fired");
    stream_.stop();
    lastSyncFailed_ = true;
    scheduleReconnect(nextBackoff(reconnectBackoffMs_));
    return;
  }
  while (stream_.available() > 0) {
    const int byte = stream_.read();
    if (byte < 0) {
      break;
    }
    handleStreamByte(static_cast<uint8_t>(byte));
  }
}

void NtfyClient::handleStreamByte(uint8_t byte) {
  streamLastDataMs_ = millis();
  switch (streamState_) {
    case StreamState::kAwaitHeader: {
      if (byte == '\n') {
        record_[recordLength_] = '\0';
        std::size_t length = recordLength_;
        while (length > 0 && record_[length - 1] == '\r') {
          --length;
        }
        record_[length] = '\0';
        if (length == 0) {
          // Blank line: the response header block is finished.
          if (!statusOk_) {
            Serial.printf("[NTFY] stream HTTP error status=%u\n", statusCode_);
            if (statusCode_ == 400 && store_ != nullptr) {
              // ntfy rejects an unknown since=<id> (e.g. a stale or synthetic
              // checkpoint). Drop it and resume fresh on the next attempt.
              if (store_->resetProcessedId()) {
                Serial.println("[NTFY] invalid since reset to latest");
              }
            }
            stream_.stop();
            lastSyncFailed_ = true;
            scheduleReconnect(nextBackoff(reconnectBackoffMs_));
            return;
          }
          streamState_ = chunked_ ? StreamState::kChunkSize
                                  : StreamState::kChunkData;
          if (!chunked_) {
            chunkRemaining_ = 0xFFFFFFFFUL;  // drain until disconnect
          }
          recordLength_ = 0;
          return;
        }
        if (std::strncmp(record_, "HTTP/", 5) == 0) {
          statusOk_ = std::strstr(record_, " 200 ") != nullptr ||
                      std::strstr(record_, " 201 ") != nullptr ||
                      std::strstr(record_, " 204 ") != nullptr;
          statusCode_ = 0;
          const char* firstSpace = std::strchr(record_, ' ');
          if (firstSpace != nullptr) {
            statusCode_ = static_cast<unsigned>(std::strtoul(firstSpace + 1,
                                                             nullptr, 10));
          }
        } else if (containsWord(record_, "chunked")) {
          chunked_ = true;
        }
        recordLength_ = 0;
      } else if (recordLength_ < sizeof(record_) - 1) {
        record_[recordLength_++] = static_cast<char>(byte);
      }
      break;
    }

    case StreamState::kChunkSize: {
      if (byte == '\n') {
        record_[recordLength_] = '\0';
        while (recordLength_ > 0 && record_[recordLength_ - 1] == '\r') {
          record_[--recordLength_] = '\0';
        }
        const char* cursor = record_;
        unsigned long value = 0;
        for (; *cursor != '\0' && *cursor != ';'; ++cursor) {
          unsigned long digit;
          if (*cursor >= '0' && *cursor <= '9') {
            digit = static_cast<unsigned long>(*cursor - '0');
          } else if (*cursor >= 'a' && *cursor <= 'f') {
            digit = static_cast<unsigned long>(*cursor - 'a' + 10);
          } else if (*cursor >= 'A' && *cursor <= 'F') {
            digit = static_cast<unsigned long>(*cursor - 'A' + 10);
          } else {
            digit = 0;
            cursor = record_;
            break;
          }
          value = value * 16 + digit;
          if (value > 0xFFFFFFF0UL) {
            value = 0xFFFFFFF0UL;
            break;
          }
        }
        if (cursor == record_) {
          stream_.stop();
          lastSyncFailed_ = true;
          scheduleReconnect(nextBackoff(reconnectBackoffMs_));
          return;
        }
        recordLength_ = 0;
        if (value == 0) {
          streamState_ = StreamState::kFinished;
          return;
        }
        chunkRemaining_ = static_cast<uint32_t>(value);
        streamState_ = StreamState::kChunkData;
      } else if (recordLength_ < sizeof(record_) - 1) {
        record_[recordLength_++] = static_cast<char>(byte);
      }
      break;
    }

    case StreamState::kChunkData: {
      if (recordOversize_) {
        // Skip until the next newline after an oversized record.
        if (byte == '\n') {
          recordOversize_ = false;
          recordLength_ = 0;
        }
      } else if (byte == '\n') {
        handleRecordLine();
        recordLength_ = 0;
      } else if (recordLength_ < sizeof(record_) - 1) {
        record_[recordLength_++] = static_cast<char>(byte);
      } else {
        recordOversize_ = true;
      }
      if (chunkRemaining_ != 0xFFFFFFFFUL) {
        if (chunkRemaining_ > 0) {
          --chunkRemaining_;
        }
        if (chunkRemaining_ == 0) {
          streamState_ = StreamState::kChunkCrLf;
        }
      }
      break;
    }

    case StreamState::kChunkCrLf: {
      if (byte == '\n') {
        streamState_ = StreamState::kChunkSize;
      }
      break;
    }

    case StreamState::kFinished: {
      // Terminal chunk seen; whatever follows is a trailer we can drop.
      stream_.stop();
      streamState_ = StreamState::kIdle;
      reconnectBackoffMs_ = 0;
      reconnectAtMs_ = millis() + 1000;
      lastSyncFailed_ = false;
      break;
    }

    case StreamState::kIdle:
    case StreamState::kConnecting:
      break;
  }
}

void NtfyClient::handleRecordLine() {
  std::size_t length = recordLength_;
  while (length > 0 && record_[length - 1] == '\r') {
    --length;
  }
  record_[length] = '\0';
  if (length == 0) {
    return;
  }
  messagelogic::InboxMessage incoming;
  const messagelogic::IngestResult result =
      messagelogic::preIngestLine(record_, length, incoming);
  switch (result) {
    case messagelogic::IngestResult::kAccepted: {
      const MessageService::AddResult added = store_->addIncoming(incoming);
      switch (added) {
        case MessageService::AddResult::kAccepted:
          store_->saveProcessedId(incoming.id);
          Serial.printf("[NTFY] stored id=%s\n", incoming.id);
          break;
        case MessageService::AddResult::kDuplicate:
          // Already known; the index still holds the id so no replay.
          break;
        case MessageService::AddResult::kStorageWriteFailed:
          Serial.println("[NTFY] store unavailable; scheduling replay");
          lastSyncFailed_ = true;
          reconnectNow();
          break;
        default:
          break;
      }
      break;
    }
    case messagelogic::IngestResult::kIgnoredEvent:
      break;
    case messagelogic::IngestResult::kMalformed:
      Serial.println("[NTFY] malformed stream record");
      break;
    case messagelogic::IngestResult::kOversizeLine:
      Serial.println("[NTFY] oversized stream line dropped");
      break;
    case messagelogic::IngestResult::kOversize:
      Serial.println("[NTFY] oversized field dropped");
      break;
  }
}

void NtfyClient::pumpAck() {
  if (ackPhase_ == AckPhase::kDone) {
    ackPhase_ = AckPhase::kIdle;
  }
  if (ackPhase_ != AckPhase::kIdle) {
    return;
  }
  if (!ackRequested_) {
    return;
  }
  if (static_cast<int32_t>(millis() - ackAtMs_) < 0) {
    return;  // still inside the retry backoff
  }
  const int index = store_->firstPendingAckIndex();
  if (index < 0) {
    ackRequested_ = false;
    return;
  }
  tryStartAck(static_cast<std::size_t>(index));
}

void NtfyClient::tryStartAck(std::size_t index) {
  ackIndex_ = index;
  ackPhase_ = AckPhase::kConnecting;
  ack_.stop();

  const MessageService::Summary& summary = store_->at(index);
  if (summary.id[0] == '\0') {
    Serial.println("[NTFY] ack dropped: empty message id");
    ackPhase_ = AckPhase::kDone;
    return;
  }
  if (!messagelogic::makeSeenAckId(summary.id, ackSequenceId_,
                                   sizeof(ackSequenceId_))) {
    Serial.println("[NTFY] ack dropped: sequence id build failed");
    ackPhase_ = AckPhase::kDone;
    ackRequested_ = false;
    return;
  }
  if (messagelogic::makeSeenAckBody(config_.ackTopic, ackSequenceId_,
                                    summary.preview, ackBody_,
                                    sizeof(ackBody_)) < 0) {
    Serial.println("[NTFY] ack dropped: body build failed");
    ackPhase_ = AckPhase::kDone;
    ackRequested_ = false;
    return;
  }
  ackBodyLength_ = std::strlen(ackBody_);

  char host[128];
  uint16_t port = 0;
  if (!splitBaseUrl(config_.baseUrl, host, sizeof(host), port)) {
    Serial.println("[NTFY] ack dropped: invalid base_url");
    ackPhase_ = AckPhase::kDone;
    ackRequested_ = false;
    return;
  }

  ack_ = WiFiClientSecure();
  if (config_.caCert == nullptr || config_.caCert[0] == '\0') {
    Serial.println("[NTFY] ack dropped: no CA cert configured");
    finishAck(false);
    return;
  }
  ack_.setCACert(config_.caCert);
  ack_.setHandshakeTimeout(kConnectTimeoutMs);
  ack_.setTimeout(kAckReadTimeoutMs);

  // The ESP32-2432S028 has only ~32KB of contiguous free heap while the
  // long-lived stream holds its mbedTLS in/out buffers. A second
  // mbedtls_ssl_setup fails with -32512. Drop the stream before opening the
  // ack socket; update() will reconnect it with since=<lastId> immediately
  // after the synchronous ack pass finishes (lossless for live messages).
  if (streamActive()) {
    stream_.stop();
    streamState_ = StreamState::kIdle;
    reconnectAtMs_ = 0;
    Serial.println("[NTFY] stream paused for ack");
  }

  if (!ack_.connect(host, port)) {
    Serial.println("[NTFY] ack publish connect failed");
    ack_.stop();
    finishAck(false);
    return;
  }

  char request[512];
  int written = std::snprintf(
      request, sizeof(request),
      "POST /%s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Content-Type: application/json\r\n"
      "X-Sequence-ID: %s\r\n"
      "Content-Length: %u\r\n"
      "User-Agent: %s\r\n",
      config_.ackTopic != nullptr && config_.ackTopic[0] != '\0'
          ? config_.ackTopic
          : config_.inboxTopic,
      host, ackSequenceId_, static_cast<unsigned>(ackBodyLength_),
      kUserAgent);
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(request)) {
    ack_.stop();
    finishAck(false);
    return;
  }
  if (config_.token != nullptr && config_.token[0] != '\0') {
    const int tokenHeader =
        std::snprintf(request + written, sizeof(request) - written,
                      "Authorization: Bearer %s\r\n", config_.token);
    if (tokenHeader <= 0 ||
        static_cast<std::size_t>(tokenHeader) >=
            sizeof(request) - static_cast<std::size_t>(written)) {
      ack_.stop();
      finishAck(false);
      return;
    }
    written += tokenHeader;
  }
  const int terminator =
      std::snprintf(request + written, sizeof(request) - written, "\r\n");
  if (terminator <= 0) {
    ack_.stop();
    finishAck(false);
    return;
  }
  written += terminator;

  ack_.write(reinterpret_cast<const uint8_t*>(request),
             static_cast<std::size_t>(written));
  ack_.write(reinterpret_cast<const uint8_t*>(ackBody_), ackBodyLength_);
  ack_.flush();

  // Read the status line within a bounded window, then close.
  ackPhase_ = AckPhase::kPublishing;
  const uint32_t deadline = millis() + kAckReadTimeoutMs;
  std::size_t lineLength = 0;
  bool done = false;
  while (!done && static_cast<int32_t>(deadline - millis()) > 0) {
    while (ack_.available() > 0) {
      const int byte = ack_.read();
      if (byte < 0) {
        done = true;
        break;
      }
      if (byte == '\n') {
        done = true;
        break;
      }
      if (byte == '\r') {
        continue;
      }
      if (lineLength + 1 < sizeof(request)) {
        request[lineLength++] = static_cast<char>(byte);
      }
    }
    if (!done) {
      delay(1);
    }
  }
  request[lineLength] = '\0';
  const bool okStatus =
      std::strncmp(request, "HTTP/", 5) == 0 &&
      (std::strstr(request, " 200 ") != nullptr ||
       std::strstr(request, " 201 ") != nullptr ||
       std::strstr(request, " 204 ") != nullptr);
  ack_.stop();
  finishAck(okStatus);
}

void NtfyClient::finishAck(bool success) {
  if (!success) {
    ++ackFailCount_;
    ackBackoffMs_ = nextBackoff(ackBackoffMs_);
    ackAtMs_ = millis() + ackBackoffMs_;
    ackPhase_ = AckPhase::kDone;
    Serial.println("[NTFY] ack publish failed; will retry");
    return;
  }
  ++ackSentCount_;
  store_->markAckSentAtIndex(ackIndex_);
  ackBackoffMs_ = 0;
  ackRequested_ = false;  // any further pending ack is reclaimed on the next
                          // pumpAck() pass
  ackPhase_ = AckPhase::kDone;
  Serial.printf("[NTFY] ack sent id=%s\n", ackSequenceId_);
}