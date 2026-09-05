#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "core/message_codec.h"

namespace adv {

class MessageRouter {
 public:
  using Handler = std::function<void(const Message&)>;

  void registerHandler(const std::string& actionId, Handler handler);
  bool route(const Message& message) const;

 private:
  std::unordered_map<std::string, Handler> handlers_;
};

}  // namespace adv

