#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MessageLogic.h"

namespace {

const char kMessageEvent[] =
    "{\"id\":\"SLiKI64DOt\",\"time\":1635528757,\"event\":\"message\","
    "\"topic\":\"inbox\",\"message\":\"Disk full\",\"title\":\"Luka\"}";

void testPreIngestAcceptsMessageEvent() {
  messagelogic::InboxMessage message;
  TEST_ASSERT_EQUAL(
      messagelogic::IngestResult::kAccepted,
      messagelogic::preIngestLine(kMessageEvent, std::strlen(kMessageEvent),
                                  message));
  TEST_ASSERT_EQUAL_STRING("SLiKI64DOt", message.id);
  TEST_ASSERT_EQUAL(static_cast<std::int64_t>(1635528757), message.time);
  TEST_ASSERT_EQUAL_STRING("Luka", message.sender);
  TEST_ASSERT_EQUAL_STRING("Disk full", message.text);
  TEST_ASSERT_FALSE(message.read);
  TEST_ASSERT_FALSE(message.seenAckSent);
}

void testPreIngestIgnoresNonMessageEvents() {
  const char* const events[] = {
      "{\"id\":\"A\",\"time\":1,\"event\":\"open\"}",
      "{\"id\":\"B\",\"time\":2,\"event\":\"keepalive\"}",
      "{\"id\":\"C\",\"time\":3,\"event\":\"poll_request\"}",
      "{\"id\":\"D\",\"time\":4,\"event\":\"message_delete\","
      "\"message\":\"gone\"}",
  };
  for (const char* event : events) {
    messagelogic::InboxMessage message;
    TEST_ASSERT_EQUAL(messagelogic::IngestResult::kIgnoredEvent,
                      messagelogic::preIngestLine(event, std::strlen(event),
                                                  message));
  }
}

void testPreIngestRejectsMalformedLines() {
  const char* const lines[] = {
      "",
      "not json",
      "{}",
      "{\"event\":\"message\"}",
      "{\"event\":\"message\",\"id\":\"x\"}",
      "{\"event\":\"message\",\"time\":5}",
      "{\"event\":\"message\",\"time\":5,\"id\":\"x\"}",
      "{\"time\":5,\"id\":\"x\",\"message\":\"m\"}",
      "{\"event\":\"message\",\"time\":-5,\"id\":\"x\",\"message\":\"m\"}",
  };
  for (const char* line : lines) {
    messagelogic::InboxMessage message;
    const messagelogic::IngestResult result =
        messagelogic::preIngestLine(line, std::strlen(line), message);
    TEST_ASSERT_TRUE(result == messagelogic::IngestResult::kMalformed ||
                     result == messagelogic::IngestResult::kIgnoredEvent);
  }
}

void testPreIngestExportsUtf8AndEscapes() {
  const char line[] =
      "{\"id\":\"abc\",\"time\":10,\"event\":\"message\","
      "\"message\":\"\\u010D\\u0161\\u017E caf\\u00E9\",\"title\":\"Mama\"}";
  messagelogic::InboxMessage message;
  TEST_ASSERT_EQUAL(messagelogic::IngestResult::kAccepted,
                    messagelogic::preIngestLine(line, std::strlen(line),
                                                message));
  TEST_ASSERT_EQUAL_STRING("Mama", message.sender);
  TEST_ASSERT_EQUAL_STRING("čšž café", message.text);
}

void testPreIngestNormalizesHeartsAndDropsVariationSelector() {
  // "A \xE2\x9D\xA4\xEF\xB8\x8F B \xE2\x99\xA5" -> two hearts (U+2764+FE0F,
  // U+2665) plus a stray variation selector, all collapsing to the token.
  const char line[] =
      "{\"id\":\"z\",\"time\":1,\"event\":\"message\",\"message\":\"A "
      "\xE2\x9D\xA4\xEF\xB8\x8F B \xE2\x99\xA5\",\"title\":\"\xE2\x99\xA5\"}";
  messagelogic::InboxMessage message;
  TEST_ASSERT_EQUAL(messagelogic::IngestResult::kAccepted,
                    messagelogic::preIngestLine(line, std::strlen(line),
                                                message));
  const char* token = messagelogic::heartTokenUtf8();
  const std::size_t tokenLength = messagelogic::kHeartTokenLength;
  TEST_ASSERT_EQUAL_STRING_LEN(token, message.sender, tokenLength);
  TEST_ASSERT_EQUAL_STRING_LEN("A ", message.text, 2);
  TEST_ASSERT_EQUAL_STRING_LEN(token, message.text + 2, tokenLength);
  TEST_ASSERT_EQUAL_STRING_LEN(" B ", message.text + 2 + tokenLength, 3);
  TEST_ASSERT_EQUAL_STRING_LEN(token,
                               message.text + 2 + tokenLength + 3, tokenLength);
  TEST_ASSERT_EQUAL('\0', message.text[2 + tokenLength + 3 + tokenLength]);
}

void testPreIngestDefaultTitleAndEmptyMessage() {
  const char line[] =
      "{\"id\":\"q\",\"time\":55,\"event\":\"message\",\"message\":\"\"}";
  messagelogic::InboxMessage message;
  TEST_ASSERT_EQUAL(messagelogic::IngestResult::kAccepted,
                    messagelogic::preIngestLine(line, std::strlen(line),
                                                message));
  TEST_ASSERT_EQUAL_STRING("Luka", message.sender);
  TEST_ASSERT_EQUAL_STRING("", message.text);
}

void testPreIngestRejectsOversizeFields() {
  char line[2048];
  std::strcpy(line, "{\"id\":\"big\",\"time\":1,\"event\":\"message\","
                    "\"message\":\"");
  for (std::size_t index = 0; index < messagelogic::kMaxTextBytes; ++index) {
    std::strcat(line, "a");
  }
  std::strcat(line, "\"}");
  messagelogic::InboxMessage message;
  const std::size_t length = std::strlen(line);
  const messagelogic::IngestResult result =
      messagelogic::preIngestLine(line, length, message);
  TEST_ASSERT_TRUE(result == messagelogic::IngestResult::kOversize ||
                   result == messagelogic::IngestResult::kOversizeLine);
}

void testContainsIdAndDedupe() {
  messagelogic::InboxMessage messages[3];
  std::strcpy(messages[0].id, "a");
  std::strcpy(messages[1].id, "b");
  std::strcpy(messages[2].id, "c");
  TEST_ASSERT_TRUE(messagelogic::containsId(messages, 3, "b"));
  TEST_ASSERT_FALSE(messagelogic::containsId(messages, 3, "z"));
  TEST_ASSERT_TRUE(messagelogic::containsId(messages, 0, "a") == false);
}

void testUnreadCount() {
  messagelogic::InboxMessage messages[3];
  messages[0].read = true;
  messages[1].read = false;
  messages[2].read = true;
  TEST_ASSERT_EQUAL(static_cast<std::size_t>(1),
                    messagelogic::unreadCount(messages, 3));
}

void testPruneDropsOldestReadFirstAndKeepsUnread() {
  messagelogic::InboxMessage messages[5];
  for (std::size_t index = 0; index < 5; ++index) {
    std::snprintf(messages[index].id, sizeof(messages[index].id), "id%zu",
                  index);
    messages[index].time = static_cast<std::int64_t>(index);
  }
  messages[0].read = true;   // time 0 (oldest read)
  messages[1].read = true;   // time 1
  messages[2].read = false;  // unread time 2
  messages[3].read = true;   // time 3
  messages[4].read = false;  // unread time 4
  const std::size_t kept = messagelogic::pruneToLimit(messages, 5, 3);
  TEST_ASSERT_EQUAL(static_cast<std::size_t>(3), kept);
  TEST_ASSERT_FALSE(messagelogic::containsId(messages, kept, "id0"));
  TEST_ASSERT_FALSE(messagelogic::containsId(messages, kept, "id1"));
  TEST_ASSERT_TRUE(messagelogic::containsId(messages, kept, "id2"));
  TEST_ASSERT_TRUE(messagelogic::containsId(messages, kept, "id4"));
}

void testPruneRefusesToDropAllUnread() {
  messagelogic::InboxMessage messages[2];
  std::strcpy(messages[0].id, "u0");
  std::strcpy(messages[1].id, "u1");
  messages[0].read = false;
  messages[1].read = false;
  messages[0].time = 0;
  messages[1].time = 1;
  TEST_ASSERT_EQUAL(static_cast<std::size_t>(2),
                    messagelogic::pruneToLimit(messages, 2, 1));
}

void testFirstPendingAckIndex() {
  messagelogic::InboxMessage messages[3];
  messages[0].read = true;
  messages[0].seenAckSent = true;
  messages[1].read = true;
  messages[1].seenAckSent = false;
  messages[2].read = false;
  TEST_ASSERT_EQUAL(1, messagelogic::firstPendingAckIndex(messages, 3));
  messages[1].seenAckSent = true;
  TEST_ASSERT_EQUAL(-1, messagelogic::firstPendingAckIndex(messages, 3));
}

void testStoreRoundtrip() {
  messagelogic::InboxMessage source;
  std::strcpy(source.id, "aB3_x");
  source.time = 1234567890;
  std::strcpy(source.sender, "Luka");
  std::strcpy(source.sender, "Luka");
  std::strcat(source.sender, "");
  std::strcpy(source.sender, "Luka");
  std::strcpy(source.text, "Zdravo čšž ");  // token appended below (idempotent)
  std::strcat(source.text, messagelogic::heartTokenUtf8());
  source.read = true;
  source.seenAckSent = false;
  char json[messagelogic::kMaxLineBytes];
  const int serialized = messagelogic::serializeMessageJson(
      source, json, sizeof(json));
  TEST_ASSERT_TRUE(serialized > 0);
  messagelogic::InboxMessage parsed;
  TEST_ASSERT_TRUE(messagelogic::parseMessageJson(json, parsed));
  TEST_ASSERT_EQUAL_STRING(source.id, parsed.id);
  TEST_ASSERT_EQUAL(source.time, parsed.time);
  TEST_ASSERT_EQUAL_STRING(source.sender, parsed.sender);
  TEST_ASSERT_EQUAL_STRING(source.text, parsed.text);
  TEST_ASSERT_TRUE(parsed.read);
  TEST_ASSERT_FALSE(parsed.seenAckSent);
}

void testStoreRoundtripEscapesStrings() {
  messagelogic::InboxMessage source;
  std::strcpy(source.id, "id");
  source.time = 1;
  std::strcpy(source.sender, "A\"B\\C");
  std::strcpy(source.text, "line1\nline2\ttab");
  char json[messagelogic::kMaxLineBytes];
  const int serialized = messagelogic::serializeMessageJson(
      source, json, sizeof(json));
  TEST_ASSERT_TRUE(serialized > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(json, "A\\\"B\\\\C"));
  messagelogic::InboxMessage parsed;
  TEST_ASSERT_TRUE(messagelogic::parseMessageJson(json, parsed));
  TEST_ASSERT_EQUAL_STRING("A\"B\\C", parsed.sender);
  TEST_ASSERT_EQUAL_STRING("line1\nline2\ttab", parsed.text);
}

void testStoreParseRejectsMalformedLines() {
  const char* const lines[] = {
      "",
      "{}",
      "{\"id\":\"x\"}",
      "{\"time\":1}",
      "{\"id\":\"x\",\"time\":1}",
      "{\"id\":\"x\",\"time\":1,\"text\":\"a\",\"read\":\"yes\"}",
  };
  for (const char* line : lines) {
    messagelogic::InboxMessage message;
    TEST_ASSERT_FALSE(messagelogic::parseMessageJson(line, message));
  }
}

void testMakeSeenAckId() {
  char output[64];
  const int length = messagelogic::makeSeenAckId("abc123", output, sizeof(output));
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_EQUAL_STRING("seen_abc123", output);
}

void testMakeSeenAckBody() {
  char output[messagelogic::kMaxLineBytes];
  const int length = messagelogic::makeSeenAckBody(
      "mateja-acks", "seen_abc", "Rad te imam ♥", output, sizeof(output));
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(output, "\"topic\":\"mateja-acks\""));
  TEST_ASSERT_NOT_NULL(
      std::strstr(output, "\"title\":\"Mateja je prebrala sporočilo\""));
  TEST_ASSERT_NOT_NULL(std::strstr(output, "\"message\":\"Rad te imam"));
  TEST_ASSERT_NOT_NULL(std::strstr(output, "\"sequence_id\":\"seen_abc\""));
  TEST_ASSERT_NOT_NULL(std::strstr(output, "\"priority\":3"));
}

void testMakeSinceParam() {
  char output[messagelogic::kMaxIdBytes + 16];
  TEST_ASSERT_TRUE(messagelogic::makeSinceParam("abc", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("since=abc", output);
  TEST_ASSERT_FALSE(messagelogic::makeSinceParam("", output, sizeof(output)));
  TEST_ASSERT_FALSE(messagelogic::makeSinceParam(nullptr, output, sizeof(output)));
}

void testTimeLabelSameDay() {
  char output[32];
  // messageLocal = 0 + 3600 = 3600s = 01:00 ; nowLocal = 7200s, same day.
  TEST_ASSERT_TRUE(messagelogic::formatMessageTimeLabel(
      0, 3600, 3600, true, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("01:00", output);
}

void testTimeLabelJustNowAndYesterday() {
  char output[32];
  TEST_ASSERT_TRUE(messagelogic::formatMessageTimeLabel(
      3550, 3600, 3600, true, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("zdaj", output);
  // "včeraj": now=172800 (day 2), message=86400 (day 1).
  TEST_ASSERT_TRUE(messagelogic::formatMessageTimeLabel(
      86400, 172800, 0, true, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("včeraj", output);
}

void testTimeLabelOlderDateAndInvalid() {
  char output[32];
  // 1970-09-13 is day 255 after the epoch.
  TEST_ASSERT_TRUE(messagelogic::formatMessageTimeLabel(
      255 * 86400LL, 1728000000LL, 0, true, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("13. sep.", output);
  TEST_ASSERT_FALSE(messagelogic::formatMessageTimeLabel(
      1000, 3600, 3600, false, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("", output);
}

void testCivilDateMath() {
  TEST_ASSERT_EQUAL(static_cast<std::int64_t>(0),
                    messagelogic::daysFromCivil(1970, 1, 1));
  TEST_ASSERT_EQUAL(static_cast<std::int64_t>(25567),
                    messagelogic::daysFromCivil(2040, 1, 1));
  int year = 0;
  int month = 0;
  int day = 0;
  messagelogic::civilFromDays(25567, year, month, day);
  TEST_ASSERT_EQUAL(2040, year);
  TEST_ASSERT_EQUAL(1, month);
  TEST_ASSERT_EQUAL(1, day);
  messagelogic::civilFromDays(0, year, month, day);
  TEST_ASSERT_EQUAL(1970, year);
  TEST_ASSERT_EQUAL(1, month);
  TEST_ASSERT_EQUAL(1, day);
  messagelogic::civilFromDays(messagelogic::daysFromCivil(2015, 12, 31),
                              year, month, day);
  TEST_ASSERT_EQUAL(2015, year);
  TEST_ASSERT_EQUAL(12, month);
  TEST_ASSERT_EQUAL(31, day);
}

void testPopupVerdict() {
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kShow,
                    messagelogic::popupVerdict(true, false, false, false, false,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kSkip,
                    messagelogic::popupVerdict(false, false, false, false, false,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kSkip,
                    messagelogic::popupVerdict(true, true, false, false, false,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kSkip,
                    messagelogic::popupVerdict(true, false, true, false, false,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kSkip,
                    messagelogic::popupVerdict(true, false, false, true, false,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kSkip,
                    messagelogic::popupVerdict(true, false, false, false, true,
                                               false, nullptr, "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kKeep,
                    messagelogic::popupVerdict(true, false, false, false, false,
                                               true, "n1", "n1"));
  TEST_ASSERT_EQUAL(messagelogic::PopupVerdict::kShow,
                    messagelogic::popupVerdict(true, false, false, false, false,
                                               true, "n1", "n2"));
}

void testCopyPreviewUtf8DoesNotSplitSequences() {
  char output[16];
  const char* input = "čšž abcdefgh";
  const std::size_t written =
      messagelogic::copyPreviewUtf8(input, output, sizeof(output), 7);
  // č(2) + š(2) + ž(2) + ' ' = 7 bytes; 'a' would exceed maxBytes.
  TEST_ASSERT_EQUAL(static_cast<std::size_t>(7), written);
  TEST_ASSERT_EQUAL_STRING("čšž ", output);
}

void testNormalizeHeartsInPlace() {
  char buffer[64];
  std::strcpy(buffer, "x \xE2\x9D\xA4\xEF\xB8\x8F y");
  const std::size_t length = messagelogic::normalizeHearts(buffer, sizeof(buffer));
  // "x " + token + " y" = 2 + 3 + 2 bytes.
  TEST_ASSERT_EQUAL(static_cast<std::size_t>(7), length);
  TEST_ASSERT_EQUAL_STRING_LEN("x ", buffer, 2);
  TEST_ASSERT_EQUAL_STRING_LEN(messagelogic::heartTokenUtf8(), buffer + 2,
                               messagelogic::kHeartTokenLength);
  TEST_ASSERT_EQUAL_STRING_LEN(" y", buffer + 5, 2);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testPreIngestAcceptsMessageEvent);
  RUN_TEST(testPreIngestIgnoresNonMessageEvents);
  RUN_TEST(testPreIngestRejectsMalformedLines);
  RUN_TEST(testPreIngestExportsUtf8AndEscapes);
  RUN_TEST(testPreIngestNormalizesHeartsAndDropsVariationSelector);
  RUN_TEST(testPreIngestDefaultTitleAndEmptyMessage);
  RUN_TEST(testPreIngestRejectsOversizeFields);
  RUN_TEST(testContainsIdAndDedupe);
  RUN_TEST(testUnreadCount);
  RUN_TEST(testPruneDropsOldestReadFirstAndKeepsUnread);
  RUN_TEST(testPruneRefusesToDropAllUnread);
  RUN_TEST(testFirstPendingAckIndex);
  RUN_TEST(testStoreRoundtrip);
  RUN_TEST(testStoreRoundtripEscapesStrings);
  RUN_TEST(testStoreParseRejectsMalformedLines);
  RUN_TEST(testMakeSeenAckId);
  RUN_TEST(testMakeSeenAckBody);
  RUN_TEST(testMakeSinceParam);
  RUN_TEST(testTimeLabelSameDay);
  RUN_TEST(testTimeLabelJustNowAndYesterday);
  RUN_TEST(testTimeLabelOlderDateAndInvalid);
  RUN_TEST(testCivilDateMath);
  RUN_TEST(testPopupVerdict);
  RUN_TEST(testCopyPreviewUtf8DoesNotSplitSequences);
  RUN_TEST(testNormalizeHeartsInPlace);
  return UNITY_END();
}