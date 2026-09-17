#include <unity.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "WifiConfig.h"

namespace {

wificonfig::Credentials parse(const char* text) {
  return wificonfig::Parse(text, std::strlen(text));
}

void testValidConfig() {
  const wificonfig::Credentials credentials =
      parse("{\"ssid\":\"home-net\",\"password\":\"secret123\"}");
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("home-net", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("secret123", credentials.password);
}

void testWhitespaceAndKeyOrderTolerated() {
  const wificonfig::Credentials credentials = parse(
      "  {\n \"password\" : \"pw\" ,\n \"ssid\" :\t\"net\" }\n");
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("net", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("pw", credentials.password);
}

void testPasswordOptional() {
  const wificonfig::Credentials credentials = parse("{\"ssid\":\"open-net\"}");
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("open-net", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("", credentials.password);
}

void testEmptySsidInvalid() {
  TEST_ASSERT_FALSE(parse("{\"ssid\":\"\",\"password\":\"pw\"}").valid);
}

void testMissingSsidInvalid() {
  TEST_ASSERT_FALSE(parse("{\"password\":\"pw\"}").valid);
}

void testNonObjectInvalid() {
  TEST_ASSERT_FALSE(parse("[\"ssid\",\"net\"]").valid);
  TEST_ASSERT_FALSE(parse("\"ssid\"").valid);
}

void testMalformedInvalid() {
  TEST_ASSERT_FALSE(parse("{\"ssid\":\"net\"").valid);
  TEST_ASSERT_FALSE(parse("{\"ssid\":net}").valid);
  TEST_ASSERT_FALSE(parse("{\"ssid\":\"net\\q\"}").valid);
}

void testEscapesDecoded() {
  const wificonfig::Credentials credentials =
      parse("{\"ssid\":\"net\",\"password\":\"a\\\"b\\\\c\"}");
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("a\"b\\c", credentials.password);
}

void testOverlongSsidInvalid() {
  char json[256];
  char ssid[100];
  std::memset(ssid, 'a', sizeof(ssid));
  ssid[80] = '\0';
  std::snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", ssid);
  TEST_ASSERT_FALSE(parse(json).valid);
}

void testLookalikeKeyNotMatched() {
  TEST_ASSERT_FALSE(parse("{\"ssid_extra\":\"net\"}").valid);
}

void testExtraKeysIgnored() {
  const wificonfig::Credentials credentials =
      parse("{\"version\":1,\"ssid\":\"net\",\"password\":\"pw\",\"x\":true}");
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("net", credentials.ssid);
}

void testEmptyAndNullInvalid() {
  TEST_ASSERT_FALSE(wificonfig::Parse(nullptr, 5).valid);
  TEST_ASSERT_FALSE(wificonfig::Parse("", 0).valid);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(testValidConfig);
  RUN_TEST(testWhitespaceAndKeyOrderTolerated);
  RUN_TEST(testPasswordOptional);
  RUN_TEST(testEmptySsidInvalid);
  RUN_TEST(testMissingSsidInvalid);
  RUN_TEST(testNonObjectInvalid);
  RUN_TEST(testMalformedInvalid);
  RUN_TEST(testEscapesDecoded);
  RUN_TEST(testOverlongSsidInvalid);
  RUN_TEST(testLookalikeKeyNotMatched);
  RUN_TEST(testExtraKeysIgnored);
  RUN_TEST(testEmptyAndNullInvalid);
  return UNITY_END();
}
