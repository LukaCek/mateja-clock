#pragma once

#include <cstddef>
#include <cstdint>

namespace messagelogic {

// Bounded model constants. Payloads exceeding these limits are discarded
// safely (never buffered unbounded, never crash).
constexpr std::size_t kMaxIdBytes = 32;
constexpr std::size_t kMaxTitleBytes = 96;
constexpr std::size_t kMaxTextBytes = 768;
constexpr std::size_t kMaxLineBytes = 1536;  // one JSON stream line budget
constexpr std::size_t kMaxMessages = 100;

// The dominant NTUTF-8 sequence is exactly (F0 9F 92 9C) (U+10000..);
// the pinching heart variants are U+2764/U+2764+FE0F/U+2665. All are
// normalized to a 3-byte private-use marker so a tiny vector heart can be
// drawn instead of embedding a full emoji font.
constexpr std::size_t kHeartTokenLength = 3;
const char* heartTokenUtf8();

struct InboxMessage {
  char id[kMaxIdBytes] = {0};
  std::int64_t time = 0;
  char sender[kMaxTitleBytes] = {0};
  char text[kMaxTextBytes] = {0};
  bool read = false;
  bool seenAckSent = false;
};

enum class IngestResult : std::uint8_t {
  kIgnoredEvent,  // open / keepalive / delete etc.: not a message payload
  kMalformed,     // malformed JSON or missing required id/time/event/message
  kOversizeLine,  // raw line exceeded the bounded buffer
  kOversize,      // title or text exceeded its field limit
  kDuplicate,     // id already present (checked by the store, not here)
  kAccepted,
};

// Interprets one line-delimited JSON event from the ntfy stream. Only
// event=="message" produces kAccepted. Output hearts are normalized.
IngestResult preIngestLine(const char* line, std::size_t length,
                           InboxMessage& output);

bool containsId(const InboxMessage* messages, std::size_t count,
                const char* id);

// Removes oldest READ messages first until count <= limit. Unread messages
// are preserved whenever possible. Returns the new count.
std::size_t pruneToLimit(InboxMessage* messages, std::size_t count,
                         std::size_t limit);

std::size_t unreadCount(const InboxMessage* messages, std::size_t count);

// First index whose message is read but not yet acknowledged, or -1.
int firstPendingAckIndex(const InboxMessage* messages, std::size_t count);

// In-place normalization of all common heart representations in a UTF-8
// buffer. Returns the new byte length. Also drops stray variation-selector
// bytes. Never shortens beyond capacity that would split a sequence.
std::size_t normalizeHearts(char* text, std::size_t capacity);

// Copies at most maxBytes UTF-8 bytes without splitting a multi-byte
// sequence, NUL-terminated.
std::size_t copyPreviewUtf8(const char* input, char* output,
                            std::size_t outputSize, std::size_t maxBytes);

// Store serialization: newest-first array objects.
int serializeMessageJson(const InboxMessage& message, char* output,
                         std::size_t size);
bool parseMessageJson(const char* line, InboxMessage& output);

// Deterministic acknowledgement identity for the original ntfy message.
int makeSeenAckId(const char* originalId, char* output, std::size_t size);

// ntfy publish JSON body for the seen acknowledgement.
int makeSeenAckBody(const char* ackTopic, const char* ackSequenceId,
                    const char* preview, char* output, std::size_t size);

// "since=<id>" parameter for the JSON stream, or empty when there is no
// id yet (the client then must use since=latest for safe initial behavior).
bool makeSinceParam(const char* lastId, char* output, std::size_t size);

// Compact relative time label: "zdaj", "HH:MM", "včeraj", "D. moj.".
// Returns true and writes the label; neutral empty label when invalid.
bool formatMessageTimeLabel(std::int64_t messageTime, std::int64_t nowEpoch,
                            std::int32_t localOffsetSeconds, bool timeValid,
                            char* output, std::size_t size);

// Popup policy: a message popup may only appear on an idle Home screen and
// never while an alarm is ringing or settings/list/detail are open.
enum class PopupVerdict : std::uint8_t { kShow, kKeep, kSkip };
PopupVerdict popupVerdict(bool homeActive, bool ringingActive,
                          bool settingsActive, bool listActive,
                          bool detailActive, bool popupShowing,
                          const char* shownId, const char* incomingId);

// Civil date helpers used by the time labels (pure).
std::int64_t daysFromCivil(int year, int month, int day);
void civilFromDays(std::int64_t days, int& year, int& month, int& day);

}  // namespace messagelogic