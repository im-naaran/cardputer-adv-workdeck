#include <unity.h>
#include "platform/ble_transport.h"
void feed(adv::BleTransport& ble,const std::string& text) {
  ble.onWriteBytes(reinterpret_cast<const uint8_t*>(text.data()),text.size());
}
void generation_reset() {
  adv::BleTransport ble; std::string value;
  ble.onConnected(); ble.poll(0);
  feed(ble,"old-part"); ble.poll(1);
  const auto old=ble.generation();
  TEST_ASSERT_TRUE(ble.enqueueRequest("old","{}",1));
  ble.onDisconnected(); ble.onConnected();
  TEST_ASSERT_NOT_EQUAL(old,ble.generation());
  feed(ble,"new\n"); ble.poll(2);
  TEST_ASSERT_TRUE(ble.takeMessage(value)); TEST_ASSERT_EQUAL_STRING("new",value.c_str());
  TEST_ASSERT_FALSE(ble.cancelPending("old"));
  for(int i=0;i<4;++i) TEST_ASSERT_TRUE(ble.enqueueRequest(std::to_string(i),"{}",2));
  TEST_ASSERT_FALSE(ble.enqueueRequest("full","{}",2));
  ble.pollTransmit(2); TEST_ASSERT_FALSE(ble.cancelPending("0"));
  TEST_ASSERT_TRUE(ble.enqueueRequest("next","{}",2));
  ble.onDisconnected(); TEST_ASSERT_FALSE(ble.takeMessage(value));
  TEST_ASSERT_FALSE(ble.enqueueRequest("off","{}",3));
}
void queued_old_chunks() {
  adv::BleTransport ble; ble.onConnected(); ble.poll(0);
  feed(ble,"old\n"); ble.onDisconnected(); ble.onConnected(); feed(ble,"new\n");
  ble.poll(1); std::string value;
  TEST_ASSERT_TRUE(ble.takeMessage(value)); TEST_ASSERT_EQUAL_STRING("new",value.c_str());
  TEST_ASSERT_FALSE(ble.takeMessage(value));
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(generation_reset); RUN_TEST(queued_old_chunks); return UNITY_END(); }
