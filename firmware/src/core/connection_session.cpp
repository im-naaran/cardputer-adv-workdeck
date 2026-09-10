#include "core/connection_session.h"

#include <algorithm>

#include "core/protocol_constants.h"

namespace adv {

void ConnectionSession::onBleConnected() {
  // BLE alone is insufficient: business state stays disconnected until hello is valid.
  disconnect();
  bleConnected_ = true;
}

bool ConnectionSession::acceptHello(const Message& message) {
  bool validTypes = true;
  std::vector<ActionType> seen;
  for (auto type : message.supportedActionTypes) {
    if ((type != ActionType::kScript && type != ActionType::kClipboard) ||
        std::find(seen.begin(), seen.end(), type) != seen.end()) validTypes = false;
    seen.push_back(type);
  }
  if (!validTypes || !bleConnected_ || message.event != MessageEvent::kResponse ||
      message.actionId != protocol::kHelloAction || message.resultCode != "OK" ||
      message.protocolVersion != protocol::kVersion) {
    const bool linkStillConnected = bleConnected_;
    disconnect();
    bleConnected_ = linkStillConnected;
    return false;
  }
  computerId_ = message.computerId;
  computerName_ = message.computerName;
  capabilities_ = message.capabilities;
  supportedActionTypes_ = message.supportedActionTypes;
  ready_ = true;
  return true;
}

void ConnectionSession::disconnect() {
  // A new computer must never inherit identity or business settings from the old link.
  bleConnected_ = false;
  ready_ = false;
  computerId_.clear();
  computerName_.clear();
  capabilities_.clear();
  supportedActionTypes_.clear();
}

bool ConnectionSession::supportsActionType(ActionType type) const {
  return std::find(supportedActionTypes_.begin(), supportedActionTypes_.end(), type) != supportedActionTypes_.end();
}

bool ConnectionSession::supports(const std::string& capability) const {
  return std::find(capabilities_.begin(), capabilities_.end(), capability) != capabilities_.end();
}

}  // namespace adv
