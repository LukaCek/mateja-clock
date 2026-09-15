#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstdint>

#include "MessageLogic.h"
#include "MessageService.h"

// Streaming subscriber for the ntfy JSON event stream (HTTP/1.1, chunked
// bodies) plus a small publish path for "seen" acknowledgements.
//
// Blocking behavior: stream and ack connects perform one bounded connect()
// call (TLS handshake) per attempt; the connect/ack state machines are
// gated by exponential backoff so the main loop is only ever stalled
// during a single handshake when the network is transitioning. All payload
// reads are non-blocking (available() polling), so a healthy connection
// never stalls the loop. No secrets are ever logged; URLs are redacted to
// host + topic only.
class NtfyClient {
 public:
  struct Config {
    const char* baseUrl;     // e.g. "https://ntfy.example.com" (no trailing /)
    const char* inboxTopic;  // topic to subscribe to
    const char* ackTopic;    // topic used for seen-acknowledgements
    const char* token;       // Bearer token, or "" when the server is open
    const char* caCert;      // PEM CA certificate for TLS verification
  };

  enum class StreamState : std::uint8_t {
    kIdle,        // wifi down, backoff pending, or never started
    kConnecting,  // resolve + TCP + TLS handshake in progress
    kAwaitHeader, // reading HTTP response headers
    kChunkSize,   // reading a chunk-size line
    kChunkData,   // consuming chunk payload bytes
    kChunkCrLf,   // consuming the CRLF that ends each chunk
    kFinished,    // terminal chunk received; draining the response tail
  };

  enum class AckPhase : std::uint8_t {
    kIdle,        // no acknowledgement in flight and nothing queued
    kConnecting,  // opening the publish connection
    kPublishing,  // request sent; awaiting the response
    kDone,        // publication finished; waiting to re-evaluate the queue
  };

  NtfyClient();

  void begin(const Config& config, MessageService& store);
  void update(bool wifiConnected);

  // Forces an early reconnect attempt (used after storage failures and by
  // the serial debug command).
  void reconnectNow();

  // Schedules a seen-acknowledgement for the first un-acked read message.
  void requestAck();
  bool ackBusy() const { return ackPhase_ != AckPhase::kIdle; }

  StreamState streamState() const { return streamState_; }
  AckPhase ackPhase() const { return ackPhase_; }
  const char* stateName() const;
  bool streamActive() const {
    switch (streamState_) {
      case StreamState::kAwaitHeader:
      case StreamState::kChunkSize:
      case StreamState::kChunkData:
      case StreamState::kChunkCrLf:
      case StreamState::kFinished:
        return true;
      default:
        return false;
    }
  }

  bool lastSyncFailed() const { return lastSyncFailed_; }
  void clearSyncFailed() { lastSyncFailed_ = false; }

  void printStatus() const;

 private:
  void scheduleReconnect(uint32_t delayMs);
  void startStreamConnect();
  void sendStreamRequest();
  void pumpStream();
  void handleStreamByte(uint8_t byte);
  void handleRecordLine();
  void updateAck(bool wifiConnected);
  void tryStartAck(std::size_t index);
  void pumpAck();
  void finishAck(bool success);

  Config config_{};
  MessageService* store_ = nullptr;

  WiFiClientSecure stream_;
  WiFiClientSecure ack_;

  StreamState streamState_ = StreamState::kIdle;
  bool chunked_ = false;
  bool statusOk_ = false;
  bool haveStatusLine_ = false;
  bool lastSyncFailed_ = false;
  unsigned statusCode_ = 0;
  uint32_t chunkRemaining_ = 0;
  uint32_t streamLastDataMs_ = 0;   // millis() when stream bytes last arrived
  uint32_t reconnectBackoffMs_ = 2000;
  uint32_t reconnectAtMs_ = 0;

  char record_[messagelogic::kMaxLineBytes + 32] = {0};
  std::size_t recordLength_ = 0;
  bool recordOversize_ = false;

  bool ackRequested_ = false;
  AckPhase ackPhase_ = AckPhase::kIdle;
  uint32_t ackBackoffMs_ = 0;
  uint32_t ackAtMs_ = 0;
  std::size_t ackIndex_ = 0;
  char ackSequenceId_[messagelogic::kMaxIdBytes + 16] = {0};
  char ackBody_[messagelogic::kMaxLineBytes + 128] = {0};
  std::size_t ackBodyLength_ = 0;
  uint32_t ackSentCount_ = 0;
  uint32_t ackFailCount_ = 0;
};