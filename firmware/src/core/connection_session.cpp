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
  if (!bleConnected_ || message.event != MessageEvent::kResponse ||
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
}

bool ConnectionSession::supports(const std::string& capability) const {
  return std::find(capabilities_.begin(), capabilities_.end(), capability) != capabilities_.end();
}

}  // namespace adv
