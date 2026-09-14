#include "WavUploader.h"

#include <SD.h>

#include <cstring>

namespace {

constexpr char kExtension[] = ".new";
constexpr char kBackupSuffix[] = ".bak";

}  // namespace

uint16_t WavUploader::crc16Xmodem(const uint8_t* data, size_t length,
                                  uint16_t crc) {
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

WavUploader::Result WavUploader::receive(Stream& serial, uint32_t expectedBytes,
                                         const char* path) {
  if (expectedBytes == 0 || expectedBytes > 8U * 1024U * 1024U) {
    return Result::kSizeError;
  }
  if (path == nullptr || path[0] == '\0' || strchr(path, '/') == nullptr) {
    return Result::kSizeError;
  }
  if (!ensureDirectory(path)) {
    return Result::kSdError;
  }

  String newPath = String(path) + kExtension;
  if (SD.exists(newPath.c_str()) && !SD.remove(newPath.c_str())) {
    return Result::kFinalizeError;
  }
  File file = SD.open(newPath.c_str(), FILE_WRITE);
  if (!file) {
    return Result::kSdError;
  }

  uint32_t remaining = expectedBytes;
  uint32_t lastByteAt = millis();
  uint16_t crc = 0;

  while (remaining > 0) {
    const size_t wanted = min<size_t>(kChunkBytes, remaining);
    size_t collected = 0;
    const uint32_t sourceStart = micros();
    while (collected < wanted) {
      const int value = serial.read();
      if (value < 0) {
        if (millis() - lastByteAt >= kStallTimeoutMs) {
          file.close();
          SD.remove(newPath.c_str());
          return Result::kTimeout;
        }
        yield();
        continue;
      }
      const uint8_t byte = static_cast<uint8_t>(value);
      chunk_[collected++] = byte;
      crc = crc16Xmodem(&byte, 1, crc);
      lastByteAt = millis();
    }
    sourceMicros_ += static_cast<uint32_t>(micros() - sourceStart);
    ++chunksWritten_;
    if (file.write(chunk_, collected) != collected) {
      file.close();
      SD.remove(newPath.c_str());
      return Result::kSdError;
    }
    remaining -= collected;
    lastReceivedBytes_ = expectedBytes - remaining;
    yield();
  }

  uint8_t crcBytes[2]{};
  for (uint8_t index = 0; index < 2; ++index) {
    int value = -1;
    while ((value = serial.read()) < 0) {
      if (millis() - lastByteAt >= kStallTimeoutMs) {
        file.close();
        SD.remove(newPath.c_str());
        return Result::kTimeout;
      }
      yield();
    }
    crcBytes[index] = static_cast<uint8_t>(value);
    lastByteAt = millis();
  }
  const uint16_t expectedCrc = static_cast<uint16_t>(crcBytes[0]) << 8 |
                               static_cast<uint16_t>(crcBytes[1]);
  file.flush();
  file.close();
  if (expectedCrc != crc) {
    SD.remove(newPath.c_str());
    return Result::kCrcError;
  }
  return finalize(path) ? Result::kSuccess : Result::kFinalizeError;
}

bool WavUploader::directoryExists(const char* path) const {
  File probe = SD.open(path, FILE_READ);
  if (!probe) {
    return false;
  }
  const bool isDirectory = probe.isDirectory();
  probe.close();
  return isDirectory;
}

bool WavUploader::ensureDirectory(const char* path) {
  const char* lastSlash = strrchr(path, '/');
  if (lastSlash == nullptr || lastSlash == path) {
    return true;
  }
  const size_t length = static_cast<size_t>(lastSlash - path);
  char directory[96];
  if (length >= sizeof(directory)) {
    return false;
  }
  memcpy(directory, path, length);
  directory[length] = '\0';

  char segment[96];
  size_t segmentLength = 0;
  for (size_t index = 0; index < length; ++index) {
    const char current = directory[index];
    if (current == '/' && segmentLength > 1) {
      segment[segmentLength] = '\0';
      if (!directoryExists(segment) && !SD.mkdir(segment)) {
        return false;
      }
    }
    if (segmentLength < sizeof(segment) - 1) {
      segment[segmentLength++] = current;
    }
  }
  if (segmentLength > 1 && directory[length - 1] != '/') {
    segment[segmentLength] = '\0';
    if (!directoryExists(segment) && !SD.mkdir(segment)) {
      return false;
    }
  }
  return true;
}

bool WavUploader::finalize(const char* path) {
  const String newPath = String(path) + kExtension;
  const String backupPath = String(path) + kBackupSuffix;
  if (SD.exists(backupPath.c_str()) && !SD.remove(backupPath.c_str())) {
    return false;
  }
  const bool hadTarget = SD.exists(path);
  if (hadTarget && !SD.rename(path, backupPath.c_str())) {
    return false;
  }
  if (!SD.rename(newPath.c_str(), path)) {
    if (hadTarget) {
      SD.rename(backupPath.c_str(), path);
    }
    return false;
  }
  if (hadTarget && !SD.remove(backupPath.c_str())) {
    // A stale backup is harmless; the new file is already in place.
  }
  return true;
}