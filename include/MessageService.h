#pragma once

#include <Arduino.h>

#include "MessageLogic.h"
#include "StatusProviders.h"

class MessageService : public MessageStatusProvider {
 public:
  static constexpr std::size_t kIndexCapacity = messagelogic::kMaxMessages;

  struct Summary {
    char id[messagelogic::kMaxIdBytes] = {0};
    std::int64_t time = 0;
    char sender[48] = {0};
    char preview[64] = {0};
    bool read = false;
    bool seenAckSent = false;
  };

  MessageService();

  void begin(bool sdMounted);

  MessageStatus status() const override;

  std::size_t count() const { return count_; }

  const Summary& at(std::size_t index) const;

  enum class AddResult : std::uint8_t {
    kAccepted,
    kDuplicate,
    kOversizeLine,
    kOversize,
    kStorageWriteFailed,
    kMalformed,
  };

  AddResult addIncoming(const messagelogic::InboxMessage& incoming);

  bool openDetail(std::size_t index, char* sender, std::size_t senderSize,
                  char* text, std::size_t textSize,
                  std::int64_t* timeOut = nullptr) const;

  bool markReadAtIndex(std::size_t index);
  bool markAckSentAtIndex(std::size_t index);
  int firstPendingAckIndex() const;

  void lastProcessedId(char* out, std::size_t size) const;
  bool saveProcessedId(const char* id);
  bool resetProcessedId();  // drop the checkpoint: next stream resumes fresh

  // Deletes all stored messages and the in-memory index. The ntfy checkpoint
  // (lastProcessedId) is intentionally preserved so the stream resumes after
  // the last accepted message and old retained messages are not replayed.
  bool clearAll();

  // Index of the message with the given id, or -1.
  int indexOfId(const char* id) const;

  void printStatus() const;
  void printList() const;

 private:
  bool loadStore();
  bool loadProcessedIndex();
  bool ensureDirectory() const;
  bool commitTmpFile(const char* path, const char* tmpPath,
                     const char* backupPath) const;
  bool persistMessages(const messagelogic::InboxMessage* newMessage = nullptr,
                       const char* pruneId = nullptr);
  void summaryFromMessage(const messagelogic::InboxMessage& incoming,
                          Summary& out);
  void addToIndex(const messagelogic::InboxMessage& incoming);
  std::size_t removeFromIndex(const char* id);
  bool hasId(const char* id) const;

  Summary index_[kIndexCapacity];
  std::size_t count_;
  bool sdMounted_;
  char lastProcessedId_[messagelogic::kMaxIdBytes] = {0};
};
