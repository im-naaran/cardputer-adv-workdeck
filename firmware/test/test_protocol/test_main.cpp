#include <unity.h>

#include <fstream>
#include <sstream>

#include "core/connection_session.h"
#include "core/message_codec.h"
#include "core/message_router.h"
#include "core/protocol_constants.h"

namespace {

std::string fixture(const char* name) {
  std::ifstream input(std::string("../protocol/fixtures/") + name);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void test_shared_fixtures_decode() {
  adv::MessageCodec codec;
  auto hello = codec.decode(fixture("hello_response.json"));
  TEST_ASSERT_TRUE(hello.ok);
  TEST_ASSERT_EQUAL(adv::protocol::kVersion, hello.message.protocolVersion);
  auto usage = codec.decode(fixture("codex_usage_success_multi_window.json"));
  TEST_ASSERT_TRUE(usage.ok);
  TEST_ASSERT_EQUAL_UINT32(3, usage.message.windows.size());
  TEST_ASSERT_TRUE(codec.decode(fixture("codex_usage_unknown_fields.json")).ok);
}

void test_hello_without_settings() {
  auto hello = adv::MessageCodec().decode(fixture("hello_without_settings.json"));
  TEST_ASSERT_TRUE(hello.ok);
  adv::ConnectionSession session;
  session.onBleConnected();
  TEST_ASSERT_TRUE(session.acceptHello(hello.message));
}

void test_missing_and_invalid_fields_rejected() {
  adv::MessageCodec codec;
  TEST_ASSERT_FALSE(codec.decode("{}").ok);
  TEST_ASSERT_FALSE(codec.decode("{\"event\":\"other\",\"actionId\":\"x\",\"execId\":\"1\"}").ok);
  TEST_ASSERT_FALSE(codec.decode(std::string(4097, 'x')).ok);
}

void test_time_contract() {
  adv::MessageCodec codec;
  auto request = codec.decode(fixture("system_time_request.json"));
  TEST_ASSERT_TRUE(request.ok);
  TEST_ASSERT_EQUAL_STRING(adv::protocol::kTimeReadAction,request.message.actionId.c_str());
  auto good = codec.decode(fixture("system_time_success.json"));
  TEST_ASSERT_TRUE(good.ok);
  TEST_ASSERT_EQUAL_INT64(1788480000123LL,good.message.epochMilliseconds);
  TEST_ASSERT_EQUAL(480,good.message.utcOffsetMinutes);
  TEST_ASSERT_FALSE(codec.decode(fixture("system_time_invalid.json")).ok);
  const std::string prefix = "{\"event\":\"response\",\"actionId\":\"system.time.read\",\"execId\":\"1\",\"result\":{\"code\":\"OK\",\"msg\":\"ok\",\"data\":";
  for (const auto* data : {"{}", "{\"epochMilliseconds\":1.5,\"utcOffsetMinutes\":0}",
      "{\"epochMilliseconds\":\"123\",\"utcOffsetMinutes\":0}",
      "{\"epochMilliseconds\":9223372036854775808,\"utcOffsetMinutes\":0}",
      "{\"epochMilliseconds\":1,\"utcOffsetMinutes\":true}",
      "{\"epochMilliseconds\":1,\"utcOffsetMinutes\":841}"}) {
    TEST_ASSERT_FALSE(codec.decode(prefix+data+"}}").ok);
  }
  TEST_ASSERT_TRUE(codec.decode(prefix+"{\"epochMilliseconds\":1,\"utcOffsetMinutes\":-720,\"future\":true}}}").ok);
  TEST_ASSERT_TRUE(codec.decode(codec.encodeTimeRequest("time-1")).ok);
}

void test_request_encoding_and_exec_id() {
  adv::MessageCodec codec;
  const std::string encoded = codec.encodeCodexUsageRequest("exec-42");
  TEST_ASSERT_EQUAL_CHAR('\n', encoded.back());
  auto decoded = codec.decode(encoded.substr(0, encoded.size() - 1));
  TEST_ASSERT_TRUE(decoded.ok);
  TEST_ASSERT_EQUAL_STRING("exec-42", decoded.message.execId.c_str());
}

void test_router_handles_known_only() {
  adv::MessageRouter router;
  int calls = 0;
  router.registerHandler(adv::protocol::kCodexUsageAction,
                         [&](const adv::Message&) { ++calls; });
  adv::Message message;
  message.actionId = adv::protocol::kCodexUsageAction;
  TEST_ASSERT_TRUE(router.route(message));
  message.actionId = "future.action";
  TEST_ASSERT_FALSE(router.route(message));
  TEST_ASSERT_EQUAL(1, calls);
}

void test_session_requires_compatible_hello_and_clears() {
  adv::MessageCodec codec;
  auto hello = codec.decode(fixture("hello_response.json"));
  adv::ConnectionSession session;
  TEST_ASSERT_FALSE(session.acceptHello(hello.message));
  session.onBleConnected();
  TEST_ASSERT_TRUE(session.acceptHello(hello.message));
  TEST_ASSERT_TRUE(session.supports(adv::protocol::kCodexUsageAction));
  session.disconnect();
  TEST_ASSERT_FALSE(session.ready());
  TEST_ASSERT_EQUAL_STRING("", session.computerName().c_str());
}

void test_incompatible_version_rejected() {
  adv::MessageCodec codec;
  auto hello = codec.decode(fixture("hello_response.json"));
  hello.message.protocolVersion = 2;
  adv::ConnectionSession session;
  session.onBleConnected();
  TEST_ASSERT_FALSE(session.acceptHello(hello.message));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_shared_fixtures_decode);
  RUN_TEST(test_hello_without_settings);
  RUN_TEST(test_time_contract);
  RUN_TEST(test_missing_and_invalid_fields_rejected);
  RUN_TEST(test_request_encoding_and_exec_id);
  RUN_TEST(test_router_handles_known_only);
  RUN_TEST(test_session_requires_compatible_hello_and_clears);
  RUN_TEST(test_incompatible_version_rejected);
  return UNITY_END();
}
