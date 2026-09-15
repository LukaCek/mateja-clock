#include "MessageService.h"

#include <SD.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr char kStorePath[] = "/clock/messages/messages.json";
constexpr char kStoreBackupPath[] = "/clock/messages/messages.json.bak";
constexpr char kStoreTmpPath[] = "/clock/messages/messages.tmp";
constexpr char kProcessedPath[] = "/clock/messages/processed.json";
constexpr char kProcessedTmpPath[] = "/clock/messages/processed.tmp";
constexpr char kProcessedBackupPath[] = "/clock/messages/processed.json.bak";
constexpr std::size_t kMessageLineBytes = 2048;
constexpr std::size_t kProcessedBufferBytes = 256;

bool writeAll(File& file, const char* data, std::size_t length) {
  return file.write(reinterpret_cast<const uint8_t*>(data), length) == length;
}

// Reads the whole file into `buffer` when it fits; returns false otherwise.
bool readWholeFile(const char* path, char* buffer, std::size_t capacity) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const std::size_t size = file.size();
  if (size == 0 || size >= capacity) {
    file.close();
    return false;
  }
  const std::size_t read = file.readBytes(buffer, size);
  file.close();
  if (read != size) {
    return false;
  }
  buffer[size] = '\0';
  return true;
}

class LineReader {
 public:
  bool open(const char* path) {
    close();
    file_ = SD.open(path, FILE_READ);
    return file_ ? true : false;
  }
  void close() {
    if (file_) {
      file_.close();
    }
  }
  // Reads one line (newline stripped, NUL-terminated). Returns true while a
  // line was produced; the final EOF with no trailing newline is still a
  // valid line, but an empty buffer after EOF terminates iteration.
  bool next(char* buffer, std::size_t capacity, std::size_t& outLength) {
    if (ended_) {
      return false;
    }
    std::size_t length = 0;
    truncated_ = false;
    for (;;) {
      const int byte = file_.read();
      if (byte < 0) {
        ended_ = true;
        break;
      }
      if (byte == '\r') {
        continue;
      }
      if (byte == '\n') {
        break;
      }
      if (length + 1 < capacity) {
        buffer[length++] = static_cast<char>(byte);
      } else {
        truncated_ = true;
      }
    }
    buffer[length] = '\0';
    outLength = length;
    if (length == 0 && ended_) {
      return false;
    }
    return true;
  }
  bool truncated() const { return truncated_; }

 private:
  File file_;
  bool ended_ = false;
  bool truncated_ = false;
};

}  // namespace

MessageService::MessageService() : count_(0), sdMounted_(false) {
  std::memset(lastProcessedId_, 0, sizeof(lastProcessedId_));
}

void MessageService::begin(bool sdMounted) {
  sdMounted_ = sdMounted;
  if (!sdMounted_) {
    Serial.println("[MSG] sd unavailable; messages disabled");
    return;
  }
  loadStore();
  loadProcessedIndex();
  Serial.printf("[MSG] restored messages=%u last_processed=%s\n",
                static_cast<unsigned>(count_),
                lastProcessedId_[0] != '\0' ? lastProcessedId_ : "(none)");
}

MessageStatus MessageService::status() const {
  MessageStatus result;
  for (std::size_t index = 0; index < count_; ++index) {
    if (!index_[index].read) {
      ++result.unreadCount;
    }
  }
  return result;
}

const MessageService::Summary& MessageService::at(std::size_t index) const {
  if (count_ == 0) {
    return index_[0];
  }
  if (index >= count_) {
    index = count_ - 1;
  }
  return index_[index];
}

MessageService::AddResult MessageService::addIncoming(
    const messagelogic::InboxMessage& incoming) {
  if (hasId(incoming.id)) {
    return AddResult::kDuplicate;
  }
  if (!sdMounted_) {
    return AddResult::kStorageWriteFailed;
  }
  char pruneId[messagelogic::kMaxIdBytes] = {0};
  bool prune = false;
  if (count_ >= kIndexCapacity) {
    // Drop the single oldest READ message first; never silently delete
    // unread ones. Passing the id to persist keeps file and index in sync.
    std::size_t oldestRead = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < count_; ++index) {
      if (!index_[index].read) {
        continue;
      }
      if (oldestRead == static_cast<std::size_t>(-1) ||
          index_[index].time < index_[oldestRead].time) {
        oldestRead = index;
      }
    }
    if (oldestRead == static_cast<std::size_t>(-1)) {
      Serial.println("[MSG] cap reached, all messages unread; retained");
      return AddResult::kStorageWriteFailed;
    }
    std::strncpy(pruneId, index_[oldestRead].id, sizeof(pruneId) - 1);
    prune = true;
  }
  if (!persistMessages(&incoming, prune ? pruneId : nullptr)) {
    Serial.println("[MSG] store write failed; replay will retry");
    return AddResult::kStorageWriteFailed;
  }
  if (prune) {
    removeFromIndex(pruneId);
  }
  addToIndex(incoming);
  return AddResult::kAccepted;
}

bool MessageService::openDetail(std::size_t index, char* sender,
                                std::size_t senderSize, char* text,
                                std::size_t textSize,
                                std::int64_t* timeOut) const {
  if (index >= count_ || sender == nullptr || text == nullptr) {
    return false;
  }
  const char* targetId = index_[index].id;
  LineReader reader;
  if (!reader.open(kStorePath)) {
    return false;
  }
  char line[kMessageLineBytes];
  std::size_t length = 0;
  while (reader.next(line, sizeof(line), length)) {
    if (length == 0 || reader.truncated()) {
      continue;
    }
    messagelogic::InboxMessage message;
    if (!messagelogic::parseMessageJson(line, message)) {
      continue;
    }
    if (std::strcmp(message.id, targetId) != 0) {
      continue;
    }
    reader.close();
    messagelogic::copyPreviewUtf8(message.sender, sender, senderSize,
                                  senderSize - 1);
    messagelogic::copyPreviewUtf8(message.text, text, textSize, textSize - 1);
    if (timeOut != nullptr) {
      *timeOut = message.time;
    }
    return true;
  }
  reader.close();
  return false;
}

bool MessageService::markReadAtIndex(std::size_t index) {
  if (index >= count_) {
    return false;
  }
  index_[index].read = true;
  if (!persistMessages(nullptr)) {
    Serial.println("[MSG] read flag persistence failed; retried later");
    return false;
  }
  return true;
}

bool MessageService::markAckSentAtIndex(std::size_t index) {
  if (index >= count_) {
    return false;
  }
  index_[index].seenAckSent = true;
  if (!persistMessages(nullptr)) {
    Serial.println("[MSG] ack flag persistence failed; retried later");
    return false;
  }
  return true;
}

int MessageService::firstPendingAckIndex() const {
  for (std::size_t index = 0; index < count_; ++index) {
    if (index_[index].read && !index_[index].seenAckSent) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void MessageService::lastProcessedId(char* out, std::size_t size) const {
  if (out == nullptr || size == 0) {
    return;
  }
  std::strncpy(out, lastProcessedId_, size - 1);
  out[size - 1] = '\0';
}

bool MessageService::saveProcessedId(const char* id) {
  if (!sdMounted_ || id == nullptr) {
    return false;
  }
  if (!ensureDirectory()) {
    return false;
  }
  char buffer[kProcessedBufferBytes];
  const int written = std::snprintf(
      buffer, sizeof(buffer), "{\"version\":1,\"lastProcessedId\":\"%s\"}", id);
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
    return false;
  }
  if (SD.exists(kProcessedTmpPath) && !SD.remove(kProcessedTmpPath)) {
    return false;
  }
  File temporary = SD.open(kProcessedTmpPath, FILE_WRITE);
  if (!temporary) {
    return false;
  }
  const bool wrote = writeAll(temporary, buffer,
                              static_cast<std::size_t>(written));
  temporary.flush();
  temporary.close();
  if (!wrote) {
    SD.remove(kProcessedTmpPath);
    return false;
  }
  char verified[kProcessedBufferBytes];
  if (!readWholeFile(kProcessedTmpPath, verified, sizeof(verified)) ||
      std::strstr(verified, id) == nullptr) {
    SD.remove(kProcessedTmpPath);
    return false;
  }
  if (!commitTmpFile(kProcessedPath, kProcessedTmpPath,
                     kProcessedBackupPath)) {
    return false;
  }
  std::strncpy(lastProcessedId_, id, sizeof(lastProcessedId_) - 1);
  lastProcessedId_[sizeof(lastProcessedId_) - 1] = '\0';
  return true;
}

bool MessageService::resetProcessedId() {
  lastProcessedId_[0] = '\0';
  return saveProcessedId(lastProcessedId_);
}

int MessageService::indexOfId(const char* id) const {
  if (id == nullptr) {
    return -1;
  }
  for (std::size_t index = 0; index < count_; ++index) {
    if (std::strcmp(index_[index].id, id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void MessageService::printStatus() const {
  MessageStatus current = status();
  Serial.printf(
      "[MSG_STATUS] count=%u unread=%u last_processed=%s sd=%s \
first_pending_ack=%d\n",
      static_cast<unsigned>(count_), static_cast<unsigned>(current.unreadCount),
      lastProcessedId_[0] != '\0' ? lastProcessedId_ : "(none)",
      sdMounted_ ? "mounted" : "unavailable", firstPendingAckIndex());
}

void MessageService::printList() const {
  for (std::size_t index = 0; index < count_; ++index) {
    Serial.printf(
        "[MSG_LIST] index=%zu read=%s ack=%s time=%lld id=%s sender=%s\n",
        index, index_[index].read ? "yes" : "no",
        index_[index].seenAckSent ? "yes" : "no",
        static_cast<long long>(index_[index].time), index_[index].id,
        index_[index].sender);
  }
}

bool MessageService::loadStore() {
  count_ = 0;
  LineReader reader;
  if (!reader.open(kStorePath)) {
    Serial.println("[MSG] store missing; starting empty");
    return false;
  }
  char line[kMessageLineBytes];
  std::size_t length = 0;
  std::size_t skipped = 0;
  while (reader.next(line, sizeof(line), length)) {
    if (length == 0 || reader.truncated()) {
      ++skipped;
      continue;
    }
    messagelogic::InboxMessage message;
    if (!messagelogic::parseMessageJson(line, message)) {
      ++skipped;
      continue;
    }
    if (hasId(message.id)) {
      ++skipped;
      continue;
    }
    if (count_ >= kIndexCapacity) {
      ++skipped;
      continue;
    }
    summaryFromMessage(message, index_[count_]);
    ++count_;
  }
  reader.close();
  if (skipped != 0) {
    Serial.printf("[MSG] store skipped=%u lines\n",
                  static_cast<unsigned>(skipped));
  }
  return count_ != 0 || skipped == 0;
}

bool MessageService::loadProcessedIndex() {
  char buffer[kProcessedBufferBytes];
  if (!sdMounted_ || !readWholeFile(kProcessedPath, buffer, sizeof(buffer))) {
    Serial.println("[MSG] processed index missing");
    return false;
  }
  const char* needle = std::strstr(buffer, "\"lastProcessedId\"");
  if (needle == nullptr) {
    return false;
  }
  const char* quote = std::strchr(needle + 17, '"');
  if (quote == nullptr) {
    return false;
  }
  ++quote;
  const char* end = std::strchr(quote, '"');
  if (end == nullptr || end == quote ||
      static_cast<std::size_t>(end - quote) >= sizeof(lastProcessedId_)) {
    return false;
  }
  std::memcpy(lastProcessedId_, quote, static_cast<std::size_t>(end - quote));
  lastProcessedId_[end - quote] = '\0';
  return true;
}

bool MessageService::ensureDirectory() const {
  if (SD.exists("/clock/messages")) {
    File directory = SD.open("/clock/messages", FILE_READ);
    const bool valid = directory && directory.isDirectory();
    directory.close();
    return valid;
  }
  if (!SD.exists("/clock") && !SD.mkdir("/clock")) {
    return false;
  }
  if (!SD.mkdir("/clock/messages") && !SD.exists("/clock/messages")) {
    return false;
  }
  return true;
}

bool MessageService::commitTmpFile(const char* path, const char* tmpPath,
                                   const char* backupPath) const {
  const bool hadFinal = SD.exists(path);
  bool backupReady = false;
  if (hadFinal) {
    if (SD.exists(backupPath) && !SD.remove(backupPath)) {
      return false;
    }
    if (!SD.rename(path, backupPath)) {
      return false;
    }
    backupReady = true;
  }
  if (!SD.rename(tmpPath, path)) {
    if (backupReady && !SD.rename(backupPath, path)) {
      Serial.println("[MSG] backup restore failed");
    }
    return false;
  }
  if (backupReady && SD.exists(backupPath) && !SD.remove(backupPath)) {
    Serial.println("[MSG] stale backup removal failed");
  }
  return true;
}

bool MessageService::persistMessages(
    const messagelogic::InboxMessage* newMessage, const char* pruneId) {
  if (!sdMounted_ || !ensureDirectory()) {
    return false;
  }

  if (SD.exists(kStoreTmpPath) && !SD.remove(kStoreTmpPath)) {
    return false;
  }
  File temporary = SD.open(kStoreTmpPath, FILE_WRITE);
  if (!temporary) {
    return false;
  }

  bool ok = true;
  std::size_t written = 0;

  auto emit = [&temporary, &ok, &written](const messagelogic::InboxMessage& m) {
    if (!ok) {
      return;
    }
    char line[kMessageLineBytes];
    const int serialized =
        messagelogic::serializeMessageJson(m, line, sizeof(line));
    if (serialized < 0) {
      ok = false;
      return;
    }
    ok = writeAll(temporary, line, static_cast<std::size_t>(serialized)) &&
         writeAll(temporary, "\n", 1);
    if (ok) {
      ++written;
    }
  };

  // Newest message is written first (index order: newest = index 0).
  if (newMessage != nullptr) {
    emit(*newMessage);
  }

  LineReader reader;
  const bool hadStore = reader.open(kStorePath);
  if (ok && hadStore) {
    char line[kMessageLineBytes];
    std::size_t length = 0;
    while (ok && reader.next(line, sizeof(line), length)) {
      if (length == 0 || reader.truncated()) {
        continue;
      }
      messagelogic::InboxMessage message;
      if (!messagelogic::parseMessageJson(line, message)) {
        continue;
      }
      if (newMessage != nullptr &&
          std::strcmp(message.id, newMessage->id) == 0) {
        continue;  // duplicate defensive skip (index dedupe is primary)
      }
      if (pruneId != nullptr && std::strcmp(message.id, pruneId) == 0) {
        continue;  // pruned message is removed from the file too
      }
      // Merge in-memory read/ack state into the stored line so flags we
      // changed after the last save are not lost.
      for (std::size_t index = 0; index < count_; ++index) {
        if (std::strcmp(index_[index].id, message.id) == 0) {
          message.read = index_[index].read;
          message.seenAckSent = index_[index].seenAckSent;
          break;
        }
      }
      emit(message);
    }
  }
  reader.close();
  temporary.flush();
  temporary.close();

  if (!ok || written == 0) {
    SD.remove(kStoreTmpPath);
    return false;
  }

  // Verify the temporary store is readable and whole before committing.
  LineReader verify;
  std::size_t verified = 0;
  if (verify.open(kStoreTmpPath)) {
    char line[kMessageLineBytes];
    std::size_t length = 0;
    while (verify.next(line, sizeof(line), length)) {
      if (length == 0 || verify.truncated()) {
        continue;
      }
      messagelogic::InboxMessage message;
      if (messagelogic::parseMessageJson(line, message)) {
        ++verified;
      }
    }
    verify.close();
  }
  if (verified != written) {
    SD.remove(kStoreTmpPath);
    return false;
  }

  if (!commitTmpFile(kStorePath, kStoreTmpPath, kStoreBackupPath)) {
    return false;
  }
  return true;
}

void MessageService::summaryFromMessage(const messagelogic::InboxMessage& in,
                                        Summary& out) {
  out = Summary{};
  std::strncpy(out.id, in.id, sizeof(out.id) - 1);
  out.id[sizeof(out.id) - 1] = '\0';
  out.time = in.time;
  messagelogic::copyPreviewUtf8(in.sender, out.sender, sizeof(out.sender),
                                sizeof(out.sender) - 2);
  out.sender[sizeof(out.sender) - 1] = '\0';
  messagelogic::copyPreviewUtf8(in.text, out.preview, sizeof(out.preview),
                                sizeof(out.preview) - 2);
  out.preview[sizeof(out.preview) - 1] = '\0';
  out.read = in.read;
  out.seenAckSent = in.seenAckSent;
}

void MessageService::addToIndex(const messagelogic::InboxMessage& incoming) {
  if (count_ >= kIndexCapacity) {
    return;
  }
  for (std::size_t index = count_; index > 0; --index) {
    index_[index] = index_[index - 1];
  }
  summaryFromMessage(incoming, index_[0]);
  ++count_;
}

std::size_t MessageService::removeFromIndex(const char* id) {
  for (std::size_t index = 0; index < count_; ++index) {
    if (std::strcmp(index_[index].id, id) == 0) {
      for (std::size_t shift = index; shift + 1 < count_; ++shift) {
        index_[shift] = index_[shift + 1];
      }
      --count_;
      return 1;
    }
  }
  return 0;
}

bool MessageService::hasId(const char* id) const {
  for (std::size_t index = 0; index < count_; ++index) {
    if (std::strcmp(index_[index].id, id) == 0) {
      return true;
    }
  }
  return false;
}