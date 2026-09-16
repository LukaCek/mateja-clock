#include "RtcLinkService.h"

#include "hardware_pins.h"

void RtcLinkService::begin(HardwareSerial& link) {
  link_ = &link;
  link_->begin(pins::kRtcUartBaud, SERIAL_8N1, pins::kRtcUartRx,
               pins::kRtcUartTx);
  Serial.printf("[RTC_LINK] link=%s baud=%lu proto=%u\n",
                &link == &Serial ? "Serial (USB)" : "dedicated UART",
                static_cast<unsigned long>(pins::kRtcUartBaud),
                static_cast<unsigned>(rtclink::kProtocolVersion));
}

void RtcLinkService::update() {
  if (link_ == nullptr) {
    return;
  }
  const std::int64_t nowMs = static_cast<std::int64_t>(millis());
  while (link_->available() > 0) {
    core_.pushByte(static_cast<std::uint8_t>(link_->read()), nowMs);
  }
  core_.onTick(nowMs);
  if (core_.hasOutFrame()) {
    link_->write(core_.outData(), core_.outLength());
    core_.clearOutFrame();
  }
}

bool RtcLinkService::connected() const {
  return core_.connected(static_cast<std::int64_t>(millis()));
}

bool RtcLinkService::protocolCompatible() const {
  return core_.protocolCompatible();
}

std::int64_t RtcLinkService::lastSeenMs() const {
  return core_.lastSeenMs();
}

bool RtcLinkService::rtcValid() const { return core_.rtcValid(); }
std::uint32_t RtcLinkService::rtcEpoch() const { return core_.rtcEpoch(); }
bool RtcLinkService::alarmSwitchKnown() const { return core_.alarmSwitchKnown(); }
bool RtcLinkService::alarmSwitchOn() const { return core_.alarmSwitchOn(); }
bool RtcLinkService::volumeKnown() const { return core_.volumeKnown(); }
std::uint8_t RtcLinkService::volumePercent() const {
  return core_.volumePercent();
}

std::uint32_t RtcLinkService::consumeSnoozePressed() {
  return core_.consumeSnoozePressed();
}

void RtcLinkService::synchronize(std::int64_t trustedEpoch) {
  core_.synchronize(trustedEpoch, static_cast<std::int64_t>(millis()));
}

bool RtcLinkService::forceSync(std::int64_t trustedEpoch) {
  return core_.forceSync(trustedEpoch, static_cast<std::int64_t>(millis()));
}

void RtcLinkService::requestStatus() {
  core_.requestStatusNow(static_cast<std::int64_t>(millis()));
}

void RtcLinkService::printStatus() const {
  Serial.printf(
      "[RTC_LINK] connected=%s compat=%s rtc=%s epoch=%u switch=%s volume=%s "
      "frames=%u accepted=%u rejected=%u crc=%u u=%u/l=%u/v=%u "
      "ping=%u status=%u writes=%u results=%u last_seen_ms=%lld\n",
      connected() ? "yes" : "no",
      core_.protocolCompatible() ? "yes" : "no",
      core_.rtcValid() ? "yes" : "no",
      static_cast<unsigned>(core_.rtcEpoch()),
      core_.alarmSwitchKnown() ? (core_.alarmSwitchOn() ? "on" : "off")
                               : "none",
      core_.volumeKnown() ? "yes" : "no",
      static_cast<unsigned>(core_.framesValid()),
      static_cast<unsigned>(core_.framesAccepted()),
      static_cast<unsigned>(core_.framesRejected()),
      static_cast<unsigned>(core_.crcErrors()),
      static_cast<unsigned>(core_.unknownTypes()),
      static_cast<unsigned>(core_.tooLongFrames()),
      static_cast<unsigned>(core_.versionMismatches()),
      static_cast<unsigned>(core_.pingsSent()),
      static_cast<unsigned>(core_.statusesSent()),
      static_cast<unsigned>(core_.rtcWritesSent()),
      static_cast<unsigned>(core_.setRtcResultsSeen()),
      static_cast<long long>(core_.lastSeenMs()));
}