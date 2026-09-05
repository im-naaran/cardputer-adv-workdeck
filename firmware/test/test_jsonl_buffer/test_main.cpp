#include <unity.h>

#include <string>

#include "core/jsonl_buffer.h"
#include "platform/ble_transport.h"

namespace {

void test_split_and_joined_lines() {
  adv::JsonlBuffer buffer;
  const std::string first = "{\"text\":\"你";
  const std::string second = "好\"}\n{\"n\":2}\n";
  TEST_ASSERT_TRUE(buffer.feed(reinterpret_cast<const uint8_t*>(first.data()), first.size(), 10).lines.empty());
  auto result = buffer.feed(reinterpret_cast<const uint8_t*>(second.data()), second.size(), 20);
  TEST_ASSERT_EQUAL_UINT32(2, result.lines.size());
  TEST_ASSERT_EQUAL_STRING("{\"text\":\"你好\"}", result.lines[0].c_str());
}

void test_utf8_can_split_inside_character() {
  adv::JsonlBuffer buffer;
  const std::string line = "{\"v\":\"中文\"}\n";
  buffer.feed(reinterpret_cast<const uint8_t*>(line.data()), 9, 0);
  auto result = buffer.feed(reinterpret_cast<const uint8_t*>(line.data() + 9), line.size() - 9, 1);
  TEST_ASSERT_EQUAL_STRING("{\"v\":\"中文\"}", result.lines[0].c_str());
}

void test_overflow_and_timeout_clear_partial_line() {
  adv::JsonlBuffer buffer(4, 100);
  const std::string tooLong = "12345";
  TEST_ASSERT_TRUE(buffer.feed(reinterpret_cast<const uint8_t*>(tooLong.data()), tooLong.size(), 1).overflowed);
  TEST_ASSERT_EQUAL_UINT32(0, buffer.size());
  const uint8_t value = 'x';
  buffer.feed(&value, 1, 0xFFFFFFF0u);
  TEST_ASSERT_TRUE(buffer.tick(0x00000060u).timedOut);
}

void test_transport_queue_and_disconnect() {
  adv::BleTransport transport(2);
  transport.onConnected();
  const std::string lines = "a\nb\n";
  transport.onWriteBytes(reinterpret_cast<const uint8_t*>(lines.data()), lines.size());
  transport.poll(5);
  std::string message;
  TEST_ASSERT_TRUE(transport.takeMessage(message));
  TEST_ASSERT_EQUAL_STRING("a", message.c_str());
  transport.onDisconnected();
  TEST_ASSERT_FALSE(transport.takeMessage(message));
  TEST_ASSERT_FALSE(transport.enqueueRequest("disconnected", "x", 10));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_split_and_joined_lines);
  RUN_TEST(test_utf8_can_split_inside_character);
  RUN_TEST(test_overflow_and_timeout_clear_partial_line);
  RUN_TEST(test_transport_queue_and_disconnect);
  return UNITY_END();
}
