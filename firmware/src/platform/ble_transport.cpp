#include "platform/ble_transport.h"

#include <algorithm>

#include "core/protocol_constants.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace adv {

#ifdef ARDUINO
namespace {
BleTransport* activeTransport = nullptr;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { if (activeTransport) activeTransport->onConnected(); }
  void onDisconnect(BLEServer*) override { if (activeTransport) activeTransport->onDisconnected(); }
};

class WriteCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    if (!activeTransport) return;
    const std::string value = characteristic->getValue();
    activeTransport->onWriteBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }
};
}  // namespace
#endif

BleTransport::BleTransport(size_t queueLimit)
    : queueLimit_(queueLimit), buffer_(protocol::kMaxJsonBytes, 5000) {}

BleTransport::~BleTransport() {
#ifdef ARDUINO
  if (activeTransport == this) activeTransport = nullptr;
  if (queueMutex_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(queueMutex_));
#endif
}

void BleTransport::begin() {
#ifdef ARDUINO
  activeTransport = this;
  queueMutex_ = xSemaphoreCreateMutex();
  BLEDevice::init("Cardputer-Adv");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(protocol::kServiceUuid);
  BLECharacteristic* notify = service->createCharacteristic(
      protocol::kAdvToPcNotifyUuid, BLECharacteristic::PROPERTY_NOTIFY);
  notify->addDescriptor(new BLE2902());
  BLECharacteristic* write = service->createCharacteristic(
      protocol::kPcToAdvWriteUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  write->setCallbacks(new WriteCallbacks());
  notifyCharacteristic_ = notify;
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(protocol::kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
#endif
}

void BleTransport::onWriteBytes(const uint8_t* data, size_t size) {
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreTake(static_cast<SemaphoreHandle_t>(queueMutex_), portMAX_DELAY);
#endif
  if (chunks_.size() >= queueLimit_) {
    ++droppedMessages_;
#ifdef ARDUINO
    if (queueMutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(queueMutex_));
#endif
    return;
  }
  chunks_.push_back({std::string(reinterpret_cast<const char*>(data), size), generation_.load()});
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(queueMutex_));
#endif
}

void BleTransport::onConnected() {
  ++generation_;
  connected_ = true;
  connectionChanged_ = true;
}

void BleTransport::onDisconnected() {
  ++generation_;
  connected_ = false;
  connectionChanged_ = true;
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreTake(static_cast<SemaphoreHandle_t>(queueMutex_), portMAX_DELAY);
#endif
  chunks_.clear();
  resetPending_ = true;
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(queueMutex_));
  BLEDevice::startAdvertising();
#endif
}

void BleTransport::poll(uint32_t nowMs) {
  const uint32_t currentGeneration = generation_.load();
  if (resetPending_.exchange(false) || polledGeneration_ != currentGeneration) {
    buffer_.clear();
    messages_.clear();
    outgoing_.clear();
    polledGeneration_ = currentGeneration;
  }
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreTake(static_cast<SemaphoreHandle_t>(queueMutex_), portMAX_DELAY);
#endif
  std::deque<IncomingChunk> chunks;
  chunks.swap(chunks_);
#ifdef ARDUINO
  if (queueMutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(queueMutex_));
#endif
  while (!chunks.empty()) {
    IncomingChunk chunk = std::move(chunks.front());
    chunks.pop_front();
    if (chunk.generation != currentGeneration) continue;
    const auto parsed = buffer_.feed(reinterpret_cast<const uint8_t*>(chunk.bytes.data()),
                                     chunk.bytes.size(), nowMs);
    if (parsed.overflowed || parsed.timedOut) ++droppedMessages_;
    for (const auto& line : parsed.lines) {
      if (messages_.size() >= queueLimit_) {
        ++droppedMessages_;
      } else {
        messages_.push_back(line);
      }
    }
  }
  if (buffer_.tick(nowMs).timedOut) ++droppedMessages_;
}

bool BleTransport::takeMessage(std::string& message) {
  if (!connected_ || generation_.load() != polledGeneration_) {
    messages_.clear();
    return false;
  }
  if (messages_.empty()) return false;
  message = std::move(messages_.front());
  messages_.pop_front();
  return true;
}

bool BleTransport::enqueueRequest(const std::string& id, const std::string& message, uint32_t nowMs) {
  if (!connected_ || generation_.load() != polledGeneration_) return false;
  return outgoing_.enqueue(id, message, nowMs, polledGeneration_);
}

void BleTransport::pollTransmit(uint32_t nowMs) {
  if (!connected_ || generation_.load() != polledGeneration_) return;
  outgoing_.poll(nowMs, polledGeneration_, [this](const std::string& chunk) {
    if (!connected_ || generation_.load() != polledGeneration_) return false;
#ifdef ARDUINO
    auto* notify = static_cast<BLECharacteristic*>(notifyCharacteristic_);
    if (!notify) return false;
    notify->setValue(chunk);
    notify->notify();
#else
    (void)chunk;
#endif
    return true;
  });
}

bool BleTransport::consumeConnectionChanged() {
  return connectionChanged_.exchange(false);
}

}  // namespace adv
