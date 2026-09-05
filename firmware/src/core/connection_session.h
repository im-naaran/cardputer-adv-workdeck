#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/message_codec.h"

namespace adv {

class ConnectionSession {
 public:
  void onBleConnected();
  bool acceptHello(const Message& message);
  void disconnect();

  bool bleConnected() const { return bleConnected_; }
  bool ready() const { return ready_; }
  const std::string& computerId() const { return computerId_; }
  const std::string& computerName() const { return computerName_; }
  bool supports(const std::string& capability) const;

 private:
  bool bleConnected_{false};
  bool ready_{false};
  std::string computerId_;
  std::string computerName_;
  std::vector<std::string> capabilities_;
};

}  // namespace adv
