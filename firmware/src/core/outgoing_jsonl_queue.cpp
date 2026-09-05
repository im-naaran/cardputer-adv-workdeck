#include "core/outgoing_jsonl_queue.h"
#include "core/protocol_constants.h"
#include "platform/monotonic_clock.h"
namespace adv {
bool OutgoingJsonlQueue::enqueue(const std::string& id, const std::string& jsonl,
                                uint32_t now, uint32_t generation) {
  if (id.empty() || count_ == frames_.size()) return false;
  std::string bytes = jsonl;
  if (!bytes.empty() && bytes.back() == '\n') bytes.pop_back();
  if (bytes.empty() || bytes.size() > protocol::kMaxJsonBytes ||
      bytes.find_first_of("\r\n") != std::string::npos) return false;
  bytes.push_back('\n');
  frames_[count_++] = {id, std::move(bytes), 0, now, generation};
  return true;
}
void OutgoingJsonlQueue::remove(size_t index) {
  for (size_t i = index + 1; i < count_; ++i) frames_[i - 1] = std::move(frames_[i]);
  frames_[--count_] = Frame{};
}
bool OutgoingJsonlQueue::cancelPending(const std::string& id) {
  for (size_t i = 0; i < count_; ++i) if (frames_[i].id == id) {
    // An active frame must reach LF before another frame can use this stream.
    if (frames_[i].offset != 0) return false;
    remove(i);
    return true;
  }
  return false;
}
void OutgoingJsonlQueue::clear() {
  while (count_) remove(count_ - 1);
  hasSent_ = false;
}
void OutgoingJsonlQueue::poll(uint32_t now, uint32_t generation, const Sink& sink) {
  for (size_t i = 0; i < count_;) {
    const auto& f = frames_[i];
    if (f.generation != generation || (f.offset == 0 && elapsedMs(now, f.queuedAt) > 10000)) remove(i);
    else ++i;
  }
  if (!count_ || (hasSent_ && elapsedMs(now, lastSentAt_) < protocol::kNotifyChunkDelayMs)) return;
  auto& f = frames_[0];
  const auto chunk = f.bytes.substr(f.offset, protocol::kSafeChunkBytes);
  // Pace failed attempts too; never spin or catch up multiple fragments in one poll.
  hasSent_ = true;
  lastSentAt_ = now;
  if (!sink(chunk)) return;
  f.offset += chunk.size();
  if (f.offset == f.bytes.size()) remove(0);
}
}
