#include "RtclinkProtocol.h"

#include <cstring>

namespace rtclink {

std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U)
                ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1);
    }
  }
  return crc;
}

std::uint8_t encodeFrame(Type type, const std::uint8_t* payload,
                         std::uint8_t payloadLength, std::uint8_t* out,
                         std::uint8_t outCapacity) {
  if (out == nullptr || outCapacity < payloadLength + 6 ||
      payloadLength > kMaxPayload) {
    return 0;
  }
  if (payloadLength > 0 && payload == nullptr) {
    return 0;
  }
  out[0] = kSof;
  out[1] = kProtocolVersion;
  out[2] = static_cast<std::uint8_t>(type);
  out[3] = payloadLength;
  for (std::uint8_t i = 0; i < payloadLength; ++i) {
    out[4 + i] = payload[i];
  }
  const std::uint16_t crc = crc16(out + 1, payloadLength + 3);
  out[4 + payloadLength] = static_cast<std::uint8_t>(crc & 0xFF);
  out[5 + payloadLength] = static_cast<std::uint8_t>(crc >> 8);
  return payloadLength + 6;
}

namespace {

std::uint8_t encodeEpoch(Type type, std::uint32_t epoch, std::uint8_t* out,
                         std::uint8_t cap) {
  std::uint8_t payload[4] = {
      static_cast<std::uint8_t>(epoch & 0xFF),
      static_cast<std::uint8_t>((epoch >> 8) & 0xFF),
      static_cast<std::uint8_t>((epoch >> 16) & 0xFF),
      static_cast<std::uint8_t>((epoch >> 24) & 0xFF)};
  return encodeFrame(type, payload, sizeof(payload), out, cap);
}

bool decodeEpoch(const Frame& frame, std::uint32_t& epoch) {
  if (frame.length != 4) {
    return false;
  }
  epoch = static_cast<std::uint32_t>(frame.payload[0]) |
          (static_cast<std::uint32_t>(frame.payload[1]) << 8) |
          (static_cast<std::uint32_t>(frame.payload[2]) << 16) |
          (static_cast<std::uint32_t>(frame.payload[3]) << 24);
  return true;
}

std::uint8_t encodeByte(Type type, std::uint8_t value, std::uint8_t* out,
                        std::uint8_t cap) {
  std::uint8_t payload[1] = {value};
  return encodeFrame(type, payload, sizeof(payload), out, cap);
}

}  // namespace

std::uint8_t encodeSetRtcTime(std::uint32_t epoch, std::uint8_t* out,
                              std::uint8_t cap) {
  return encodeEpoch(Type::kSetRtcTime, epoch, out, cap);
}

std::uint8_t encodeRequestStatus(std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(Type::kRequestStatus, nullptr, 0, out, cap);
}

std::uint8_t encodePing(std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(Type::kPing, nullptr, 0, out, cap);
}

std::uint8_t encodeRtcTime(std::uint32_t epoch, std::uint8_t* out,
                           std::uint8_t cap) {
  return encodeEpoch(Type::kRtcTime, epoch, out, cap);
}

std::uint8_t encodeRtcInvalid(std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(Type::kRtcInvalid, nullptr, 0, out, cap);
}

std::uint8_t encodeSnoozePressed(std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(Type::kSnoozePressed, nullptr, 0, out, cap);
}

std::uint8_t encodeAlarmSwitch(bool on, std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(on ? Type::kAlarmSwitchOn : Type::kAlarmSwitchOff, nullptr,
                     0, out, cap);
}

std::uint8_t encodeVolumeChanged(std::uint8_t volume, std::uint8_t* out,
                                 std::uint8_t cap) {
  return encodeByte(Type::kVolumeChanged, volume, out, cap);
}

std::uint8_t encodeStatus(const Status& status, std::uint8_t* out,
                          std::uint8_t cap) {
  std::uint8_t payload[kStatusPayloadLen] = {0};
  payload[0] = status.protocolVersion;
  payload[1] = status.rtcValid ? 1 : 0;
  payload[2] = static_cast<std::uint8_t>(status.rtcEpoch & 0xFF);
  payload[3] = static_cast<std::uint8_t>((status.rtcEpoch >> 8) & 0xFF);
  payload[4] = static_cast<std::uint8_t>((status.rtcEpoch >> 16) & 0xFF);
  payload[5] = static_cast<std::uint8_t>((status.rtcEpoch >> 24) & 0xFF);
  payload[6] = status.alarmSwitchOn ? 1 : 0;
  payload[7] = status.volume;
  payload[8] = static_cast<std::uint8_t>(status.uptimeSeconds & 0xFF);
  payload[9] = static_cast<std::uint8_t>((status.uptimeSeconds >> 8) & 0xFF);
  payload[10] = static_cast<std::uint8_t>((status.uptimeSeconds >> 16) & 0xFF);
  payload[11] = static_cast<std::uint8_t>((status.uptimeSeconds >> 24) & 0xFF);
  return encodeFrame(Type::kStatus, payload, sizeof(payload), out, cap);
}

std::uint8_t encodePong(std::uint8_t* out, std::uint8_t cap) {
  return encodeFrame(Type::kPong, nullptr, 0, out, cap);
}

std::uint8_t encodeSetRtcResult(std::uint8_t result, std::uint8_t* out,
                                std::uint8_t cap) {
  return encodeByte(Type::kSetRtcResult, result, out, cap);
}

bool decodeSetRtcTime(const Frame& frame, std::uint32_t& epoch) {
  if (frame.type != Type::kSetRtcTime) {
    return false;
  }
  return decodeEpoch(frame, epoch);
}

bool decodeRtcTime(const Frame& frame, std::uint32_t& epoch) {
  if (frame.type != Type::kRtcTime) {
    return false;
  }
  return decodeEpoch(frame, epoch);
}

bool decodeVolumeChanged(const Frame& frame, std::uint8_t& volume) {
  if (frame.type != Type::kVolumeChanged) {
    return false;
  }
  if (frame.length != 1) {
    return false;
  }
  volume = frame.payload[0];
  return volume <= 100;
}

bool decodeSetRtcResult(const Frame& frame, std::uint8_t& result) {
  if (frame.type != Type::kSetRtcResult) {
    return false;
  }
  if (frame.length != 1) {
    return false;
  }
  result = frame.payload[0];
  return result <= kSetRtcVerifyMismatch;
}

bool decodeStatus(const Frame& frame, Status& status) {
  if (frame.type != Type::kStatus) {
    return false;
  }
  if (frame.length != kStatusPayloadLen) {
    return false;
  }
  status.protocolVersion = frame.payload[0];
  status.rtcValid = frame.payload[1] != 0;
  status.rtcEpoch =
      static_cast<std::uint32_t>(frame.payload[2]) |
      (static_cast<std::uint32_t>(frame.payload[3]) << 8) |
      (static_cast<std::uint32_t>(frame.payload[4]) << 16) |
      (static_cast<std::uint32_t>(frame.payload[5]) << 24);
  status.alarmSwitchOn = frame.payload[6] != 0;
  status.volume = frame.payload[7];
  status.uptimeSeconds =
      static_cast<std::uint32_t>(frame.payload[8]) |
      (static_cast<std::uint32_t>(frame.payload[9]) << 8) |
      (static_cast<std::uint32_t>(frame.payload[10]) << 16) |
      (static_cast<std::uint32_t>(frame.payload[11]) << 24);
  return true;
}

FrameParser::FrameParser(std::uint8_t maxPayload) : maxPayload_(maxPayload) {
  reset();
}

void FrameParser::discard() {
  state_ = State::kSof;
  length_ = 0;
  payloadRead_ = 0;
  crcLow_ = 0;
}

void FrameParser::reset() {
  discard();
  std::memset(payloadBuffer_, 0, sizeof(payloadBuffer_));
  frame_.type = Type::kPing;
  frame_.length = 0;
  std::memset(frame_.payload, 0, sizeof(frame_.payload));
}

ParseStatus FrameParser::push(std::uint8_t byte) {
  switch (state_) {
    case State::kSof:
      if (byte == kSof) {
        state_ = State::kVersion;
      }
      return ParseStatus::kIncomplete;

    case State::kVersion:
      if (byte != kProtocolVersion) {
        discard();
        return ParseStatus::kVersionMismatch;
      }
      state_ = State::kType;
      return ParseStatus::kIncomplete;

    case State::kType:
      frame_.type = static_cast<Type>(byte);
      state_ = State::kLength;
      return ParseStatus::kIncomplete;

    case State::kLength:
      length_ = byte;
      if (length_ > maxPayload_) {
        discard();
        return ParseStatus::kMaxFrameExceeded;
      }
      payloadRead_ = 0;
      if (length_ == 0) {
        state_ = State::kCrcLow;
      } else {
        state_ = State::kPayload;
      }
      return ParseStatus::kIncomplete;

    case State::kPayload:
      payloadBuffer_[payloadRead_++] = byte;
      if (payloadRead_ == length_) {
        state_ = State::kCrcLow;
      }
      return ParseStatus::kIncomplete;

    case State::kCrcLow:
      crcLow_ = byte;
      state_ = State::kCrcHigh;
      return ParseStatus::kIncomplete;

    case State::kCrcHigh: {
      const std::uint16_t received =
          static_cast<std::uint16_t>(crcLow_) |
          static_cast<std::uint16_t>(byte) << 8;

      std::uint8_t body[3 + kMaxPayload];
      body[0] = kProtocolVersion;
      body[1] = static_cast<std::uint8_t>(frame_.type);
      body[2] = length_;
      std::memcpy(body + 3, payloadBuffer_, length_);
      const std::uint16_t expected = crc16(body, length_ + 3);

      const Type type = frame_.type;
      // Copy the payload out before resetting state for the next frame.
      if (received == expected) {
        frame_.length = length_;
        std::memcpy(frame_.payload, payloadBuffer_, length_);
      }
      discard();
      if (received != expected) {
        return ParseStatus::kCrcError;
      }
      switch (type) {
        case Type::kSetRtcTime:
        case Type::kRequestStatus:
        case Type::kPing:
        case Type::kRtcTime:
        case Type::kRtcInvalid:
        case Type::kSnoozePressed:
        case Type::kAlarmSwitchOn:
        case Type::kAlarmSwitchOff:
        case Type::kVolumeChanged:
        case Type::kStatus:
        case Type::kPong:
        case Type::kSetRtcResult:
          return ParseStatus::kFrame;
      }
      return ParseStatus::kUnknownType;
    }
  }
  return ParseStatus::kIncomplete;
}

}  // namespace rtclink