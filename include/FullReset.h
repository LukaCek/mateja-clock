#ifndef MATEJA_CLOCK_FULLRESET_H
#define MATEJA_CLOCK_FULLRESET_H

namespace fullreset {

// The ONLY paths that the full-reset SD wipe may leave untouched. Everything
// else (messages, config/state, audio, emoji, temporary/recovery files) is
// eligible for deletion and subsequent rebuild.
extern const char kPhotoDirectory[];  // "/clock/photos"
extern const char kManifestPath[];    // "/clock/manifest.json"

// True for "/clock/photos" itself or any path beneath it. A rigid directory
// prefix check: "/clock/photos.jpg" or "/clock/photos2/keep.jpg" are NOT
// photo paths.
bool isPhotoPath(const char* path);

// True only for the exact photo manifest/index file used for those photos.
bool isManifestPath(const char* path);

// True when a path must never be selected for deletion: the photo subtree,
// the photo manifest, and the root guards ("" and "/"). The container
// "/clock" itself is NOT protected; it survives only because it still holds
// protected content after the wipe.
bool isProtectedPath(const char* path);

}  // namespace fullreset

#endif  // MATEJA_CLOCK_FULLRESET_H