#include "FullReset.h"

#include <cstring>

namespace fullreset {

const char kPhotoDirectory[] = "/clock/photos";
const char kManifestPath[] = "/clock/manifest.json";
const char kNtfyConfigPath[] = "/clock/config/ntfy.json";
const char kAdminConfigPath[] = "/clock/config/admin.json";
const char kOtaConfigPath[] = "/clock/config/ota.json";

namespace {
constexpr std::size_t kPhotoDirectoryLength =
    sizeof(kPhotoDirectory) - 1;  // "/clock/photos"
}  // namespace

bool isPhotoPath(const char* path) {
  if (path == nullptr) {
    return false;
  }
  if (std::strncmp(path, kPhotoDirectory, kPhotoDirectoryLength) != 0) {
    return false;
  }
  const char next = path[kPhotoDirectoryLength];
  return next == '\0' || next == '/';
}

bool isManifestPath(const char* path) {
  if (path == nullptr) {
    return false;
  }
  return std::strcmp(path, kManifestPath) == 0;
}

bool isProtectedPath(const char* path) {
  if (path == nullptr) {
    return true;
  }
  if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
    return true;  // never remove the mount root or an empty path
  }
  return isPhotoPath(path) || isManifestPath(path) ||
         std::strcmp(path, kNtfyConfigPath) == 0 ||
         std::strcmp(path, kAdminConfigPath) == 0 ||
         std::strcmp(path, kOtaConfigPath) == 0;
}

}  // namespace fullreset
