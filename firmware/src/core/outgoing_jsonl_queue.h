#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
namespace adv {
class OutgoingJsonlQueue {
 public:
  using Sink = std::function<bool(const std::string&)>;
  bool enqueue(const std::string& id, const std::string& jsonl, uint32_t now, uint32_t generation);
  bool cancelPending(const std::string& id);
  void poll(uint32_t now, uint32_t generation, const Sink& sink);
  void clear();
  size_t size() const { return count_; }
 private:
  struct Frame { std::string id, bytes; size_t offset{0}; uint32_t queuedAt{0}, generation{0}; };
  void remove(size_t index);
  std::array<Frame, 4> frames_{};
  size_t count_{0};
  bool hasSent_{false};
  uint32_t lastSentAt_{0};
};
}
