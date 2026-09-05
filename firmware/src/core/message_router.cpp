#include "core/message_router.h"

namespace adv {

void MessageRouter::registerHandler(const std::string& actionId, Handler handler) {
  handlers_[actionId] = std::move(handler);
}

bool MessageRouter::route(const Message& message) const {
  const auto found = handlers_.find(message.actionId);
  if (found == handlers_.end()) return false;
  found->second(message);
  return true;
}

}  // namespace adv

