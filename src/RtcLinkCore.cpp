#include "RtcLinkCore.h"

namespace {

std::int64_t absoluteDifference(std::int64_t left, std::int64_t right) {
  const std::int64_t delta = left > right ? left - right : right - left;
  return delta < 0 ? -delta : delta;
}

}  // namespace

void RtcLinkCore::pushByte(std::uint8_t byte, std::int64_t nowMs) {
  handleParseStatus(parser_.push(byte), nowMs);
}

void RtcLinkCore::onTick(std::int64_t nowMs) {
  if (pendingSetRtc_ && nowMs - setRtcQueuedAtMs_ >= kSetRtcTimeoutMs) {
    pendingSetRtc_ = false;
  }

  const bool nowConnected = connected(nowMs);
  if (nowConnected && !wasConnected_) {
    wantStatusAtTick_ = true;  // Handshake (also fires on reconnect).
  }
  wasConnected_ = nowConnected;

  if (outLength_ != 0) {
    return;  // One frame in flight at a time.
  }
  if (firstTick_ || wantStatusAtTick_) {
    wantStatusAtTick_ = false;
    firstTick_ = false;
    emitStatus(nowMs);
  } else if (nowMs - lastStatusAtMs_ >= kStatusIntervalMs) {
    emitStatus(nowMs);
  } else if (nowMs - lastPingAtMs_ >= kPingIntervalMs) {
    emitPing(nowMs);
  }
}

void RtcLinkCore::sendPingNow(std::int64_t nowMs) {
  if (outLength_ == 0) {
    emitPing(nowMs);
  } else {
    wantStatusAtTick_ = false;
  }
}

void RtcLinkCore::requestStatusNow(std::int64_t) {
  wantStatusAtTick_ = true;
}

bool RtcLinkCore::setRtcTime(std::uint32_t epoch, std::int64_t nowMs) {
  if (epoch < static_cast<std::uint32_t>(kMinimumEpoch) || outLength_ != 0 ||
      pendingSetRtc_) {
    return false;
  }
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t length =
      rtclink::encodeSetRtcTime(epoch, buffer, sizeof(buffer));
  if (length == 0 || !queueFrame(buffer, length)) {
    return false;
  }
  pendingSetRtc_ = true;
  setRtcQueuedAtMs_ = nowMs;
  ++rtcWritesSent_;
  return true;
}

void RtcLinkCore::synchronize(std::int64_t trustedEpoch, std::int64_t nowMs) {
  if (trustedEpoch < kMinimumEpoch || !connected(nowMs)) {
    return;
  }
  const bool due =
      !syncEvaluated_ || nowMs - lastSyncCheckAtMs_ >= kSyncIntervalMs;
  if (!due) {
    return;
  }
  syncEvaluated_ = true;
  lastSyncCheckAtMs_ = nowMs;

  if (!rtcValid_) {
    setRtcTime(static_cast<std::uint32_t>(trustedEpoch), nowMs);
    return;
  }
  if (absoluteDifference(static_cast<std::int64_t>(rtcEpoch_),
                         trustedEpoch) >= kSyncThresholdSec) {
    setRtcTime(static_cast<std::uint32_t>(trustedEpoch), nowMs);
  }
}

bool RtcLinkCore::forceSync(std::int64_t trustedEpoch, std::int64_t nowMs) {
  if (trustedEpoch < kMinimumEpoch) {
    return false;
  }
  return setRtcTime(static_cast<std::uint32_t>(trustedEpoch), nowMs);
}

bool RtcLinkCore::connected(std::int64_t nowMs) const {
  return lastSeenAtMs_ != 0 && nowMs - lastSeenAtMs_ < kLinkTimeoutMs;
}

std::uint32_t RtcLinkCore::consumeSnoozePressed() {
  const std::uint32_t count = snoozePending_;
  snoozePending_ = 0;
  return count;
}

void RtcLinkCore::handleParseStatus(rtclink::ParseStatus status,
                                    std::int64_t nowMs) {
  switch (status) {
    case rtclink::ParseStatus::kFrame:
      ++framesValid_;
      lastSeenAtMs_ = nowMs;
      handleFrame(parser_.frame(), nowMs);
      break;
    case rtclink::ParseStatus::kCrcError:
      ++crcErrors_;
      ++framesRejected_;
      break;
    case rtclink::ParseStatus::kUnknownType:
      ++unknownTypes_;
      ++framesRejected_;
      break;
    case rtclink::ParseStatus::kVersionMismatch:
      ++versionMismatches_;
      ++framesRejected_;
      compatible_ = false;
      break;
    case rtclink::ParseStatus::kMaxFrameExceeded:
      ++tooLongFrames_;
      ++framesRejected_;
      break;
    case rtclink::ParseStatus::kIncomplete:
      break;
  }
}

void RtcLinkCore::handleFrame(const rtclink::Frame& frame, std::int64_t) {
  switch (frame.type) {
    case rtclink::Type::kRtcTime: {
      std::uint32_t epoch = 0;
      if (rtclink::decodeRtcTime(frame, epoch)) {
        rtcEpoch_ = epoch;
        rtcValid_ = true;
        ++framesAccepted_;
      } else {
        ++framesRejected_;
      }
      break;
    }
    case rtclink::Type::kRtcInvalid:
      rtcValid_ = false;
      rtcEpoch_ = 0;
      break;
    case rtclink::Type::kAlarmSwitchOn:
      switchOn_ = true;
      switchKnown_ = true;
      break;
    case rtclink::Type::kAlarmSwitchOff:
      switchOn_ = false;
      switchKnown_ = true;
      break;
    case rtclink::Type::kVolumeChanged: {
      std::uint8_t volume = 0;
      if (rtclink::decodeVolumeChanged(frame, volume)) {
        volume_ = volume;
        volumeKnown_ = true;
      } else {
        ++framesRejected_;
      }
      break;
    }
    case rtclink::Type::kSnoozePressed:
      ++snoozePending_;
      break;
    case rtclink::Type::kStatus: {
      rtclink::Status status;
      if (rtclink::decodeStatus(frame, status)) {
        rtcValid_ = status.rtcValid;
        rtcEpoch_ = status.rtcEpoch;
        switchOn_ = status.alarmSwitchOn;
        switchKnown_ = true;
        if (status.volume <= 100) {
          volume_ = status.volume;
          volumeKnown_ = true;
        }
        compatible_ = status.protocolVersion == rtclink::kProtocolVersion;
        ++framesAccepted_;
      } else {
        ++framesRejected_;
      }
      break;
    }
    case rtclink::Type::kPong:
      break;
    case rtclink::Type::kSetRtcResult: {
      std::uint8_t result = 0;
      if (rtclink::decodeSetRtcResult(frame, result)) {
        ++setRtcResultsSeen_;
      }
      pendingSetRtc_ = false;
      setRtcQueuedAtMs_ = 0;
      break;
    }
    // The C3 should never send CYD-direction request frames.
    default:
      ++framesRejected_;
      break;
  }
}

void RtcLinkCore::emitStatus(std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t length =
      rtclink::encodeRequestStatus(buffer, sizeof(buffer));
  if (length != 0 && queueFrame(buffer, length)) {
    lastStatusAtMs_ = nowMs;
    ++statusesSent_;
  }
}

void RtcLinkCore::emitPing(std::int64_t nowMs) {
  std::uint8_t buffer[rtclink::kMaxFrame];
  const std::uint8_t length = rtclink::encodePing(buffer, sizeof(buffer));
  if (length != 0 && queueFrame(buffer, length)) {
    lastPingAtMs_ = nowMs;
    ++pingsSent_;
  }
}

bool RtcLinkCore::queueFrame(const std::uint8_t* data, std::uint8_t length) {
  if (length == 0 || outLength_ != 0 || length > sizeof(out_)) {
    return false;
  }
  for (std::uint8_t index = 0; index < length; ++index) {
    out_[index] = data[index];
  }
  outLength_ = length;
  return true;
}