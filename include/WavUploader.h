#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

class WavUploader {
 public:
  enum class Result {
    kSuccess,
    kSdError,
    kTimeout,
    kCrcError,
    kSizeError,
    kFinalizeError,
  };

  // Blocks, consuming exactly expectedBytes plus two trailing CRC16-XModem
  // bytes from the given Stream, replacing the alarm WAV on success.
  Result receive(Stream& serial, uint32_t expectedBytes, const char* path);

  uint32_t lastReceivedBytes() const { return lastReceivedBytes_; }
  uint32_t sourceMicros() const { return sourceMicros_; }
  uint16_t chunksWritten() const { return chunksWritten_; }

  static uint16_t crc16Xmodem(const uint8_t* data, size_t length,
                              uint16_t crc = 0);

 private:
  static constexpr size_t kChunkBytes = 512;
  static constexpr uint32_t kStallTimeoutMs = 10000;

  bool ensureDirectory(const char* path);
  bool directoryExists(const char* path) const;
  bool finalize(const char* path);

  uint32_t lastReceivedBytes_ = 0;
  uint32_t sourceMicros_ = 0;
  uint16_t chunksWritten_ = 0;
  uint8_t chunk_[kChunkBytes];
};