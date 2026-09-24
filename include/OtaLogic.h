#pragma once

#include <cstddef>
#include <cstdint>

namespace otalogic {

constexpr std::uint32_t kMaxVersionComponent = 65535;
constexpr std::size_t kMaxProductBytes = 32;
constexpr std::size_t kMaxVersionBytes = 24;
constexpr std::size_t kMaxFirmwareBytes = 64;
constexpr std::size_t kSha256HexBytes = 64;
constexpr std::uint32_t kMinimumFirmwareBytes = 65536;

struct SemVer {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;
};

bool parseSemVer(const char* text, SemVer& output);
int compareSemVer(const SemVer& left, const SemVer& right);

struct Manifest {
  char product[kMaxProductBytes] = {0};
  char version[kMaxVersionBytes] = {0};
  char firmware[kMaxFirmwareBytes] = {0};
  std::uint32_t size = 0;
  char sha256[kSha256HexBytes + 1] = {0};
};

// Parses one flat JSON object containing exactly the five manifest fields.
bool parseManifest(const char* json, Manifest& output);

enum class ManifestError : std::uint8_t {
  kNone,
  kInvalidProduct,
  kInvalidVersion,
  kInvalidFirmware,
  kInvalidSize,
  kInvalidSha256,
};

ManifestError validateManifest(const Manifest& manifest,
                               std::uint32_t slotCapacity);

enum class UpdateDecision : std::uint8_t {
  kInvalidManifest,
  kDowngrade,
  kUpToDate,
  kUpdateAvailable,
};

UpdateDecision decideUpdate(const Manifest& manifest, const SemVer& current,
                            std::uint32_t slotCapacity);

enum class OtaState : std::uint8_t {
  kIdle,
  kChecking,
  kDownloading,
  kReadyToReboot,
  kRebootDeferred,
  kCancelled,
  kFailed,
};

enum class OtaEvent : std::uint8_t {
  kCheckRequested,
  kUpdateAvailable,
  kNoUpdate,
  kDownloadSucceeded,
  kFailure,
  kAlarmStarted,
  kAlarmStopped,
  kReset,
};

OtaState transition(OtaState state, OtaEvent event);
bool shouldCancelTransfer(OtaState state, OtaEvent event);
bool shouldReboot(OtaState state, bool alarmActive);

}  // namespace otalogic
