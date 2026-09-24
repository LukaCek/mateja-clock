#include <unity.h>

#include <cstdio>
#include <cstring>

#include "NtfyConfig.h"

namespace {

ntfyconfig::Config parse(const char* text) {
  return ntfyconfig::Parse(text, std::strlen(text));
}

void testValidConfig() {
  const ntfyconfig::Config config = parse(
      "{\"base_url\":\"https://ntfy.example.com\","
      "\"inbox_topic\":\"love-inbox\",\"ack_topic\":\"love-acks\","
      "\"access_token\":\"secret\"}");
  TEST_ASSERT_TRUE(config.valid);
  TEST_ASSERT_EQUAL_STRING("https://ntfy.example.com", config.baseUrl);
  TEST_ASSERT_EQUAL_STRING("love-inbox", config.inboxTopic);
  TEST_ASSERT_EQUAL_STRING("love-acks", config.ackTopic);
  TEST_ASSERT_EQUAL_STRING("secret", config.accessToken);
  TEST_ASSERT_EQUAL_STRING("", config.caCert);
}

void testEmptyAckUsesInboxAndEmptyTokenAllowed() {
  const ntfyconfig::Config config = parse(
      "{\"access_token\":\"\",\"ack_topic\":\"\","
      "\"inbox_topic\":\"inbox\",\"base_url\":\"https://host:8443\"}");
  TEST_ASSERT_TRUE(config.valid);
  TEST_ASSERT_EQUAL_STRING("inbox", config.ackTopic);
  TEST_ASSERT_EQUAL_STRING("", config.accessToken);
}

void testCertificateAndEscapesDecoded() {
  const ntfyconfig::Config config = parse(
      "{\"base_url\":\"https://ntfy.example\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"a\\\"b\\\\c\","
      "\"ca_cert\":\"-----BEGIN\\nLINE\\n-----END\"}");
  TEST_ASSERT_TRUE(config.valid);
  TEST_ASSERT_EQUAL_STRING("a\"b\\c", config.accessToken);
  TEST_ASSERT_EQUAL_STRING("-----BEGIN\nLINE\n-----END", config.caCert);
}

void testWhitespaceOrderAndUnknownValuesAllowed() {
  const ntfyconfig::Config config = parse(
      " { \"extra\" : {\"nested\":[true, null, -1.5e2]}, "
      "\"ack_topic\":\"ack\", \"base_url\":\"https://host\", "
      "\"access_token\":\"token\", \"inbox_topic\":\"inbox\" } \n");
  TEST_ASSERT_TRUE(config.valid);
}

void testUnicodeEscapeDecoded() {
  const ntfyconfig::Config config = parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"love-\\u2764\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}");
  TEST_ASSERT_TRUE(config.valid);
  TEST_ASSERT_EQUAL_STRING("love-\xE2\x9D\xA4", config.inboxTopic);
}

void testRequiredFieldsEnforced() {
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\"}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
}

void testHttpsBaseUrlEnforced() {
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"http://host\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host/path\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host:bad\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://[::1]\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
}

void testHttpControlCharactersRejected() {
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"inbox\\nother\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"token\\r\\nBad: x\"}").valid);
}

void testMalformedAndTrailingInputRejected() {
  TEST_ASSERT_FALSE(parse("[]").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":null}").valid);
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"inbox_topic\":\"inbox\","
      "\"ack_topic\":\"ack\",\"access_token\":\"\"}x").valid);
}

void testDuplicateKnownFieldRejected() {
  TEST_ASSERT_FALSE(parse(
      "{\"base_url\":\"https://host\",\"base_url\":\"https://other\","
      "\"inbox_topic\":\"inbox\",\"ack_topic\":\"ack\","
      "\"access_token\":\"\"}").valid);
}

void testBoundedFields() {
  char topic[ntfyconfig::kMaxTopicBytes + 1];
  std::memset(topic, 'a', sizeof(topic));
  topic[sizeof(topic) - 1] = '\0';
  char json[512];
  std::snprintf(json, sizeof(json),
                "{\"base_url\":\"https://host\",\"inbox_topic\":\"%s\","
                "\"ack_topic\":\"ack\",\"access_token\":\"\"}", topic);
  TEST_ASSERT_FALSE(parse(json).valid);
}

void testNullAndOversizedDocumentRejected() {
  TEST_ASSERT_FALSE(ntfyconfig::Parse(nullptr, 10).valid);
  TEST_ASSERT_FALSE(ntfyconfig::Parse("", 0).valid);
  TEST_ASSERT_FALSE(ntfyconfig::Parse("{}", ntfyconfig::kMaxConfigBytes + 1).valid);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(testValidConfig);
  RUN_TEST(testEmptyAckUsesInboxAndEmptyTokenAllowed);
  RUN_TEST(testCertificateAndEscapesDecoded);
  RUN_TEST(testWhitespaceOrderAndUnknownValuesAllowed);
  RUN_TEST(testUnicodeEscapeDecoded);
  RUN_TEST(testRequiredFieldsEnforced);
  RUN_TEST(testHttpsBaseUrlEnforced);
  RUN_TEST(testHttpControlCharactersRejected);
  RUN_TEST(testMalformedAndTrailingInputRejected);
  RUN_TEST(testDuplicateKnownFieldRejected);
  RUN_TEST(testBoundedFields);
  RUN_TEST(testNullAndOversizedDocumentRejected);
  return UNITY_END();
}
