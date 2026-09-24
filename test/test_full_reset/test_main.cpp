#include <unity.h>

#include <cstddef>
#include <cstring>

#include "FullReset.h"

namespace {

void testPhotoDirItselfIsProtected() {
  TEST_ASSERT_TRUE(fullreset::isPhotoPath("/clock/photos"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/photos"));
}

void testPhotoFileIsProtected() {
  TEST_ASSERT_TRUE(fullreset::isPhotoPath("/clock/photos/a.jpg"));
  TEST_ASSERT_TRUE(fullreset::isPhotoPath("/clock/photos/photo_0001.jpg"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/photos/a.jpg"));
}

void testPhotoNestedSubdirectoryIsProtected() {
  TEST_ASSERT_TRUE(fullreset::isPhotoPath("/clock/photos/sub/a.jpg"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/photos/sub/a.jpg"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/photos/sub"));
}

void testPhotoPrefixLookalikesAreNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/clock/photos2/keep.jpg"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/photos2/keep.jpg"));
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/clock/photos.jpg"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/photos.jpg"));
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/clock/photos-sibling.jpg"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/photos-sibling.jpg"));
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/clockphoto.jpg"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clockphoto.jpg"));
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/photos/a.jpg"));
}

void testManifestIsProtectedOnlyExact() {
  TEST_ASSERT_TRUE(fullreset::isManifestPath("/clock/manifest.json"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/manifest.json"));
  TEST_ASSERT_FALSE(fullreset::isManifestPath("/clock/manifest.json.bak"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/manifest.json.bak"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/manifest2.json"));
}

void testMessagesAreNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/messages/messages.json"));
  TEST_ASSERT_FALSE(
      fullreset::isProtectedPath("/clock/messages/messages.tmp"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/messages"));
}

void testEmojiAreNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/emoji/32/test.raw"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/emoji/48/2764.raw"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/emoji"));
}

void testAudioIsNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/audio/alarm.wav"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/audio/alarm_7.wav"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/audio"));
}

void testSdConfigStateIsNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config/settings.json"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config/settings.tmp"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config"));
}

void testSecretRuntimeConfigsAreProtectedOnlyExact() {
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/config/ntfy.json"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/config/admin.json"));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/clock/config/ota.json"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config/ntfy.json.bak"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config/admin.json.tmp"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/config/wifi.json"));
}

void testRecoveryAndTempFilesAreNotProtected() {
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/FSCK0001.REC"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/FSCK0037.REC"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/.cyd_diagnostic.tmp"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock/.bad_manifest.tmp"));
}

void testRootAndContainerAreRemovableGuardOnly() {
  TEST_ASSERT_TRUE(fullreset::isProtectedPath(""));
  TEST_ASSERT_TRUE(fullreset::isProtectedPath("/"));
  // The /clock container itself is NOT protected; it survives only because it
  // holds protected content after the wipe.
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock"));
  TEST_ASSERT_FALSE(fullreset::isProtectedPath("/clock"));
  TEST_ASSERT_FALSE(fullreset::isPhotoPath("/clock"));
  TEST_ASSERT_FALSE(fullreset::isManifestPath("/clock"));
}

void testNullIsTreatedAsProtected() {
  TEST_ASSERT_TRUE(fullreset::isProtectedPath(nullptr));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(testPhotoDirItselfIsProtected);
  RUN_TEST(testPhotoFileIsProtected);
  RUN_TEST(testPhotoNestedSubdirectoryIsProtected);
  RUN_TEST(testPhotoPrefixLookalikesAreNotProtected);
  RUN_TEST(testManifestIsProtectedOnlyExact);
  RUN_TEST(testMessagesAreNotProtected);
  RUN_TEST(testEmojiAreNotProtected);
  RUN_TEST(testAudioIsNotProtected);
  RUN_TEST(testSdConfigStateIsNotProtected);
  RUN_TEST(testSecretRuntimeConfigsAreProtectedOnlyExact);
  RUN_TEST(testRecoveryAndTempFilesAreNotProtected);
  RUN_TEST(testRootAndContainerAreRemovableGuardOnly);
  RUN_TEST(testNullIsTreatedAsProtected);
  return UNITY_END();
}
