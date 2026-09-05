#pragma once

#include <cstdint>
#include <atomic>
#include <deque>
#include <string>

#include "core/jsonl_buffer.h"
#include "core/outgoing_jsonl_queue.h"

namespace adv {

class BleTransport {
 public:
  explicit BleTransport(size_t queueLimit = 8);
  ~BleTransport();

  void begin();
  void poll(uint32_t nowMs);
  bool takeMessage(std::string& message);
  bool enqueueRequest(const std::string& id, const std::string& message, uint32_t nowMs);
  bool cancelPending(const std::string& id) { return outgoing_.cancelPending(id); }
  void pollTransmit(uint32_t nowMs);
  uint32_t generation() const { return generation_.load(); }
  bool connected() const { return connected_.load(); }
  bool consumeConnectionChanged();
  uint32_t droppedMessages() const { return droppedMessages_.load(); }

  // Platform callbacks only enqueue bytes/state; parsing and business work stay in loop().
  void onWriteBytes(const uint8_t* data, size_t size);
  void onConnected();
  void onDisconnected();

 private:
  struct IncomingChunk {
    std::string bytes;
    uint32_t generation;
  };

  size_t queueLimit_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> connectionChanged_{false};
  std::atomic<uint32_t> droppedMessages_{0};
  std::atomic<bool> resetPending_{false};
  JsonlBuffer buffer_;
  std::deque<IncomingChunk> chunks_;
  std::deque<std::string> messages_;
  OutgoingJsonlQueue outgoing_;
  std::atomic<uint32_t> generation_{0};
  uint32_t polledGeneration_{0};
  void* notifyCharacteristic_{nullptr};
  void* queueMutex_{nullptr};
};

}  // namespace adv
