#include <unity.h>

#include <cstdint>
#include <cstring>

#include "OtaLogic.h"

namespace {

const char kSha[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

otalogic::Manifest validManifest(const char* version = "1.2.3") {
  otalogic::Manifest manifest;
  std::strcpy(manifest.product, "mateja-clock");
  std::strcpy(manifest.version, version);
  std::strcpy(manifest.firmware, "mateja-clock.bin");
  manifest.size = 1048576;
  std::strcpy(manifest.sha256, kSha);
  return manifest;
}

otalogic::SemVer version(const char* text) {
  otalogic::SemVer parsed;
  TEST_ASSERT_TRUE(otalogic::parseSemVer(text, parsed));
  return parsed;
}

void testSemVerAcceptsExactNumericTriples() {
  otalogic::SemVer parsed;
  TEST_ASSERT_TRUE(otalogic::parseSemVer("0.0.0", parsed));
  TEST_ASSERT_EQUAL_UINT32(0, parsed.major);
  TEST_ASSERT_TRUE(otalogic::parseSemVer("65535.10.999", parsed));
  TEST_ASSERT_EQUAL_UINT32(65535, parsed.major);
  TEST_ASSERT_EQUAL_UINT32(10, parsed.minor);
  TEST_ASSERT_EQUAL_UINT32(999, parsed.patch);
}

void testSemVerRejectsAnythingButExactBoundedTriples() {
  const char* const invalid[] = {nullptr, "",       "1",       "1.2",
                                 "1.2.3.4", "v1.2.3", "1.2.3-beta",
                                 " 1.2.3",  "1.2.3 ", "01.2.3",
                                 "1.-2.3",  "1.a.3",  "65536.0.0"};
  for (const char* text : invalid) {
    otalogic::SemVer parsed;
    TEST_ASSERT_FALSE(otalogic::parseSemVer(text, parsed));
  }
}

void testSemVerComparisonIsNumericByComponent() {
  TEST_ASSERT_TRUE(otalogic::compareSemVer(version("1.0.1"),
                                           version("1.0.0")) > 0);
  TEST_ASSERT_TRUE(otalogic::compareSemVer(version("1.1.0"),
                                           version("1.0.9")) > 0);
  TEST_ASSERT_TRUE(otalogic::compareSemVer(version("1.10.0"),
                                           version("1.9.0")) > 0);
  TEST_ASSERT_TRUE(otalogic::compareSemVer(version("1.9.9"),
                                           version("2.0.0")) < 0);
  TEST_ASSERT_EQUAL(0, otalogic::compareSemVer(version("2.3.4"),
                                               version("2.3.4")));
}

void testManifestParserAcceptsCompactJsonAndAnyFieldOrder() {
  const char json[] =
      "{\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef"
      "0123456789abcdef\",\"size\":123456,\"firmware\":\"mateja-clock.bin\","
      "\"version\":\"1.10.0\",\"product\":\"mateja-clock\"}";
  otalogic::Manifest manifest;
  TEST_ASSERT_TRUE(otalogic::parseManifest(json, manifest));
  TEST_ASSERT_EQUAL_STRING("mateja-clock", manifest.product);
  TEST_ASSERT_EQUAL_STRING("1.10.0", manifest.version);
  TEST_ASSERT_EQUAL_STRING("mateja-clock.bin", manifest.firmware);
  TEST_ASSERT_EQUAL_UINT32(123456, manifest.size);
  TEST_ASSERT_EQUAL_STRING(kSha, manifest.sha256);
}

void testManifestParserAcceptsJsonWhitespace() {
  const char json[] =
      " { \"product\" : \"mateja-clock\", \"version\" : \"1.0.0\", "
      "\"firmware\" : \"mateja-clock.bin\", \"size\" : 1, "
      "\"sha256\" : \"0123456789abcdef0123456789abcdef0123456789abcdef"
      "0123456789abcdef\" } \n";
  otalogic::Manifest manifest;
  TEST_ASSERT_TRUE(otalogic::parseManifest(json, manifest));
}

void testManifestParserRejectsMalformedMissingDuplicateAndExtraFields() {
  const char* const invalid[] = {
      nullptr,
      "",
      "[]",
      "{\"product\":\"mateja-clock\"}",
      "{\"product\":\"mateja-clock\",\"product\":\"mateja-clock\","
      "\"version\":\"1.0.0\",\"firmware\":\"mateja-clock.bin\","
      "\"size\":1,\"sha256\":\"x\"}",
      "{\"product\":\"mateja-clock\",\"version\":\"1.0.0\","
      "\"firmware\":\"mateja-clock.bin\",\"size\":-1,\"sha256\":\"x\"}",
      "{\"product\":\"mateja-clock\",\"version\":\"1.0.0\","
      "\"firmware\":\"mateja-clock.bin\",\"size\":1,\"sha256\":\"x\","
      "\"extra\":true}",
      "{\"product\":\"mateja-clock\",\"version\":\"1.0.0\","
      "\"firmware\":\"mateja-clock.bin\",\"size\":4294967296,"
      "\"sha256\":\"x\"}",
  };
  for (const char* json : invalid) {
    otalogic::Manifest manifest;
    TEST_ASSERT_FALSE(otalogic::parseManifest(json, manifest));
  }
}

void testManifestValidationAcceptsExpectedValuesAtSlotLimit() {
  otalogic::Manifest manifest = validManifest();
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kNone,
                    otalogic::validateManifest(manifest, manifest.size));
}

void testManifestValidationRejectsWrongProductAndFirmware() {
  otalogic::Manifest manifest = validManifest();
  std::strcpy(manifest.product, "other");
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidProduct,
                    otalogic::validateManifest(manifest, 2000000));
  manifest = validManifest();
  std::strcpy(manifest.firmware, "other.bin");
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidFirmware,
                    otalogic::validateManifest(manifest, 2000000));
}

void testManifestValidationRejectsInvalidVersionAndSize() {
  otalogic::Manifest manifest = validManifest("1.2");
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidVersion,
                    otalogic::validateManifest(manifest, 2000000));
  manifest = validManifest();
  manifest.size = 0;
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSize,
                    otalogic::validateManifest(manifest, 2000000));
  manifest.size = otalogic::kMinimumFirmwareBytes - 1;
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSize,
                    otalogic::validateManifest(manifest, 2000000));
  manifest.size = 2000001;
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSize,
                    otalogic::validateManifest(manifest, 2000000));
}

void testManifestValidationRequiresExactly64HexShaCharacters() {
  otalogic::Manifest manifest = validManifest();
  manifest.sha256[63] = '\0';
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSha256,
                    otalogic::validateManifest(manifest, 2000000));
  manifest = validManifest();
  manifest.sha256[12] = 'g';
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSha256,
                    otalogic::validateManifest(manifest, 2000000));
  manifest = validManifest();
  manifest.sha256[12] = 'A';
  TEST_ASSERT_EQUAL(otalogic::ManifestError::kInvalidSha256,
                    otalogic::validateManifest(manifest, 2000000));
}

void testUpdateDecisionDistinguishesInvalidDowngradeEqualityAndUpdate() {
  const otalogic::SemVer current = version("1.9.0");
  TEST_ASSERT_EQUAL(otalogic::UpdateDecision::kDowngrade,
                    otalogic::decideUpdate(validManifest("1.8.9"), current,
                                           2000000));
  TEST_ASSERT_EQUAL(otalogic::UpdateDecision::kUpToDate,
                    otalogic::decideUpdate(validManifest("1.9.0"), current,
                                           2000000));
  TEST_ASSERT_EQUAL(otalogic::UpdateDecision::kUpdateAvailable,
                    otalogic::decideUpdate(validManifest("1.10.0"), current,
                                           2000000));
  TEST_ASSERT_EQUAL(otalogic::UpdateDecision::kInvalidManifest,
                    otalogic::decideUpdate(validManifest("1.10.0"), current,
                                           100));
}

void testStateProgressesThroughCheckDownloadAndReboot() {
  otalogic::OtaState state = otalogic::OtaState::kIdle;
  state = otalogic::transition(state, otalogic::OtaEvent::kCheckRequested);
  TEST_ASSERT_EQUAL(otalogic::OtaState::kChecking, state);
  state = otalogic::transition(state, otalogic::OtaEvent::kUpdateAvailable);
  TEST_ASSERT_EQUAL(otalogic::OtaState::kDownloading, state);
  state = otalogic::transition(state, otalogic::OtaEvent::kDownloadSucceeded);
  TEST_ASSERT_EQUAL(otalogic::OtaState::kReadyToReboot, state);
  TEST_ASSERT_TRUE(otalogic::shouldReboot(state, false));
}

void testNoUpdateAndFailureHaveTerminalTransitions() {
  TEST_ASSERT_EQUAL(
      otalogic::OtaState::kIdle,
      otalogic::transition(otalogic::OtaState::kChecking,
                           otalogic::OtaEvent::kNoUpdate));
  TEST_ASSERT_EQUAL(
      otalogic::OtaState::kFailed,
      otalogic::transition(otalogic::OtaState::kDownloading,
                           otalogic::OtaEvent::kFailure));
  TEST_ASSERT_EQUAL(
      otalogic::OtaState::kIdle,
      otalogic::transition(otalogic::OtaState::kFailed,
                           otalogic::OtaEvent::kReset));
}

void testAlarmCancelsCheckingAndDownloading() {
  TEST_ASSERT_TRUE(otalogic::shouldCancelTransfer(
      otalogic::OtaState::kChecking, otalogic::OtaEvent::kAlarmStarted));
  TEST_ASSERT_TRUE(otalogic::shouldCancelTransfer(
      otalogic::OtaState::kDownloading, otalogic::OtaEvent::kAlarmStarted));
  TEST_ASSERT_EQUAL(
      otalogic::OtaState::kCancelled,
      otalogic::transition(otalogic::OtaState::kDownloading,
                           otalogic::OtaEvent::kAlarmStarted));
  TEST_ASSERT_FALSE(otalogic::shouldCancelTransfer(
      otalogic::OtaState::kReadyToReboot,
      otalogic::OtaEvent::kAlarmStarted));
}

void testAlarmDefersRebootUntilItStops() {
  otalogic::OtaState state = otalogic::transition(
      otalogic::OtaState::kReadyToReboot, otalogic::OtaEvent::kAlarmStarted);
  TEST_ASSERT_EQUAL(otalogic::OtaState::kRebootDeferred, state);
  TEST_ASSERT_FALSE(otalogic::shouldReboot(state, false));
  state = otalogic::transition(state, otalogic::OtaEvent::kAlarmStopped);
  TEST_ASSERT_EQUAL(otalogic::OtaState::kReadyToReboot, state);
  TEST_ASSERT_FALSE(otalogic::shouldReboot(state, true));
  TEST_ASSERT_TRUE(otalogic::shouldReboot(state, false));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(testSemVerAcceptsExactNumericTriples);
  RUN_TEST(testSemVerRejectsAnythingButExactBoundedTriples);
  RUN_TEST(testSemVerComparisonIsNumericByComponent);
  RUN_TEST(testManifestParserAcceptsCompactJsonAndAnyFieldOrder);
  RUN_TEST(testManifestParserAcceptsJsonWhitespace);
  RUN_TEST(testManifestParserRejectsMalformedMissingDuplicateAndExtraFields);
  RUN_TEST(testManifestValidationAcceptsExpectedValuesAtSlotLimit);
  RUN_TEST(testManifestValidationRejectsWrongProductAndFirmware);
  RUN_TEST(testManifestValidationRejectsInvalidVersionAndSize);
  RUN_TEST(testManifestValidationRequiresExactly64HexShaCharacters);
  RUN_TEST(testUpdateDecisionDistinguishesInvalidDowngradeEqualityAndUpdate);
  RUN_TEST(testStateProgressesThroughCheckDownloadAndReboot);
  RUN_TEST(testNoUpdateAndFailureHaveTerminalTransitions);
  RUN_TEST(testAlarmCancelsCheckingAndDownloading);
  RUN_TEST(testAlarmDefersRebootUntilItStops);
  return UNITY_END();
}
