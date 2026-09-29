#pragma once

#include <cstddef>
#include <cstdint>

// Framed binary UART protocol shared by the Mateja Clock CYD and its ESP32-C3
// coprocessor. Pure C++ so the same code runs on the ESP32 and on the host
// test runner (identical implementation on both ends is required).
//
// Frame layout (all multi-byte integers little-endian):
//
//   SOF 0xAA | VERSION(1) | TYPE(1) | LENGTH(1) | PAYLOAD(L) | CRC16(2)
//
// CRC16 is CRC-16/CCITT-FALSE computed over VERSION..PAYLOAD (everything after
// SOF). The parser is a state machine that survives partial frames, boot
// garbage, duplicate bytes, bad CRC, unknown types and incompatible versions
// without ever getting permanently out of sync.
namespace rtclink {

constexpr std::uint8_t kSof = 0xAA;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::uint8_t kMaxPayload = 40;   // largest allowed PAYLOAD
constexpr std::uint8_t kMaxFrame = kMaxPayload + 6;  // SOF..CRC16

enum class Type : std::uint8_t {
  // CYD -> C3
  kSetRtcTime = 0x01,      // payload: u32 UTC epoch
  kRequestStatus = 0x02,   // payload: none
  kPing = 0x03,            // payload: none
  // C3 -> CYD
  kRtcTime = 0x10,         // payload: u32 UTC epoch
  kRtcInvalid = 0x11,      // payload: none
  kSnoozePressed = 0x12,   // payload: none
  kAlarmSwitchOn = 0x13,   // payload: none
  kAlarmSwitchOff = 0x14,  // payload: none
  kVolumeChanged = 0x15,   // payload: u8 0..100
  kStatus = 0x16,          // payload: Status
  kPong = 0x17,            // payload: none
  kSetRtcResult = 0x18,    // payload: u8 SetRtcResult
};

// SET_RTC_RESULT payload values.
enum SetRtcResult : std::uint8_t {
  kSetRtcOk = 0,
  kSetRtcInvalidEpoch = 1,
  kSetRtcWriteFailed = 2,
  kSetRtcVerifyMismatch = 3,
};

// STATUS payload:
//   protocol version (u8)
//   rtc valid (u8: 0/1)
//   rtc epoch (u32, 0 when invalid)
//   alarm switch (u8: 0/1)
//   volume (u8: 0..100)
//   uptime seconds (u32)
constexpr std::uint8_t kStatusPayloadLen = 12;

struct Frame {
  Type type = Type::kPing;
  std::uint8_t length = 0;
  std::uint8_t payload[kMaxPayload];
};

struct Status {
  std::uint8_t protocolVersion = kProtocolVersion;
  bool rtcValid = false;
  std::uint32_t rtcEpoch = 0;
  bool alarmSwitchOn = false;
  std::uint8_t volume = 0;
  std::uint32_t uptimeSeconds = 0;
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection).
// Known vector: crc16("123456789") == 0x29B1.
std::uint16_t crc16(const std::uint8_t* data, std::size_t length);

// Builds a frame starting at out. Returns bytes written (0 on error).
std::uint8_t encodeFrame(Type type, const std::uint8_t* payload,
                         std::uint8_t payloadLength, std::uint8_t* out,
                         std::uint8_t outCapacity);

// Convenience encoders. Each returns bytes written (0 on error).
std::uint8_t encodeSetRtcTime(std::uint32_t epoch, std::uint8_t* out,
                              std::uint8_t cap);
std::uint8_t encodeRequestStatus(std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodePing(std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodeRtcTime(std::uint32_t epoch, std::uint8_t* out,
                           std::uint8_t cap);
std::uint8_t encodeRtcInvalid(std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodeSnoozePressed(std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodeAlarmSwitch(bool on, std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodeVolumeChanged(std::uint8_t volume, std::uint8_t* out,
                                 std::uint8_t cap);
std::uint8_t encodeStatus(const Status& status, std::uint8_t* out,
                          std::uint8_t cap);
std::uint8_t encodePong(std::uint8_t* out, std::uint8_t cap);
std::uint8_t encodeSetRtcResult(std::uint8_t result, std::uint8_t* out,
                                std::uint8_t cap);

// Decoders. Return false when the frame is nil, the wrong size, or otherwise
// malformed for that message.
bool decodeSetRtcTime(const Frame& frame, std::uint32_t& epoch);
bool decodeRtcTime(const Frame& frame, std::uint32_t& epoch);
bool decodeVolumeChanged(const Frame& frame, std::uint8_t& volume);
bool decodeSetRtcResult(const Frame& frame, std::uint8_t& result);
bool decodeStatus(const Frame& frame, Status& status);

enum class ParseStatus {
  kIncomplete,      // need more bytes
  kFrame,           // a complete, valid frame is available
  kCrcError,        // CRC mismatch; parser has resynced, discard this frame
  kUnknownType,     // structurally valid but unknown TYPE; discard
  kVersionMismatch, // unsupported protocol version; parser resynced
  kMaxFrameExceeded // frame too long; parser resynced
};

// Incremental, resynchronizing frame parser. Feed one byte at a time.
class FrameParser {
 public:
  explicit FrameParser(std::uint8_t maxPayload = kMaxPayload);
  ParseStatus push(std::uint8_t byte);
  const Frame& frame() const { return frame_; }
  void reset();

 private:
  enum class State {
    kSof,
    kVersion,
    kType,
    kLength,
    kPayload,
    kCrcLow,
    kCrcHigh
  };

  void discard();

  State state_ = State::kSof;
  const std::uint8_t maxPayload_;
  std::uint8_t length_ = 0;
  std::uint8_t payloadRead_ = 0;
  std::uint16_t crcLow_ = 0;
  std::uint8_t payloadBuffer_[kMaxPayload];
  Frame frame_;
};

}  // namespace rtclink