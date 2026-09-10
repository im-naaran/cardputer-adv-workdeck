#include <unity.h>
#include <ArduinoJson.h>

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
  auto hello = codec.decode(fixture("v2/hello.json"));
  TEST_ASSERT_TRUE(hello.ok);
  TEST_ASSERT_EQUAL(adv::protocol::kVersion, hello.message.protocolVersion);
  auto usage = codec.decode(fixture("codex_usage_success_multi_window.json"));
  TEST_ASSERT_TRUE(usage.ok);
  TEST_ASSERT_EQUAL_UINT32(3, usage.message.windows.size());
  TEST_ASSERT_TRUE(codec.decode(fixture("codex_usage_unknown_fields.json")).ok);
}

void test_hello_without_settings() {
  auto hello = adv::MessageCodec().decode(fixture("v2/hello.json"));
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

std::string compact(JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}
void test_script_contract() {
  adv::MessageCodec codec;
  auto first = codec.decode(fixture("v2/script_page_first.json"));
  TEST_ASSERT_TRUE(first.ok);
  TEST_ASSERT_EQUAL(8, first.message.actions.size());
  TEST_ASSERT_EQUAL(9, first.message.total);
  TEST_ASSERT_TRUE(first.message.hasNextOffset);
  TEST_ASSERT_EQUAL_STRING("n", first.message.actions[0].effectiveKey.c_str());
  TEST_ASSERT_TRUE(first.message.actions[1].effectiveKey.empty());
  auto last = codec.decode(fixture("v2/script_page_last.json"));
  TEST_ASSERT_TRUE(last.ok);
  TEST_ASSERT_FALSE(last.message.hasNextOffset);
  TEST_ASSERT_TRUE(codec.decode(fixture("v2/script_page_empty.json")).ok);
  TEST_ASSERT_FALSE(codec.decode(fixture("v2/invalid_page.json")).ok);
  for (auto name : {"v2/script_execute_success.json", "v2/script_execute_error.json", "v2/script_shortcut_success.json"}) {
    auto result = codec.decode(fixture(name));
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("script.test.0", result.message.executedActionId.c_str());
  }
  JsonDocument doc;
  deserializeJson(doc, fixture("v2/script_page_first.json"));
  doc["result"]["data"]["total"] = true;
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  deserializeJson(doc, fixture("v2/script_page_first.json"));
  doc["result"]["data"]["actions"][0]["key"] = "G";
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  deserializeJson(doc, fixture("v2/script_page_first.json"));
  doc["result"]["data"]["actions"][1]["actionId"] = doc["result"]["data"]["actions"][0]["actionId"];
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  deserializeJson(doc, fixture("v2/script_page_empty.json"));
  doc["result"]["data"].remove("nextOffset");
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  deserializeJson(doc, fixture("v2/script_execute_success.json"));
  doc["result"]["data"]["exitCode"] = true;
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  doc["result"]["data"]["exitCode"] = 1;
  TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  doc["result"]["data"]["exitCode"] = 0;
  doc["result"]["data"]["future"] = true;
  TEST_ASSERT_TRUE(codec.decode(compact(doc)).ok);
}
void test_script_metadata_does_not_truncate_embedded_nul() {
  adv::MessageCodec codec;
  for (const char* field : {"actionId", "name", "key"}) {
    JsonDocument doc;
    deserializeJson(doc, fixture("v2/script_page_first.json"));
    const std::string prefix = field == std::string("actionId") ? "script.test.0" :
                               field == std::string("name") ? "Valid" : "g";
    const std::string value = prefix + std::string(1, '\0') + "suffix";
    doc["result"]["data"]["actions"][0][field] = value;
    TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  }
}
void test_script_requests_and_maximum_page() {
  adv::MessageCodec codec;
  JsonDocument doc;
  deserializeJson(doc, codec.encodeActionsListRequest("page", adv::ActionType::kScript, 8));
  TEST_ASSERT_EQUAL_STRING("actions.list", doc["actionId"].as<const char*>());
  TEST_ASSERT_EQUAL(8, doc["payload"]["offset"].as<int>());
  deserializeJson(doc, codec.encodeActionExecuteRequest("exec", "script.google.open"));
  TEST_ASSERT_EQUAL_STRING("actions.execute", doc["actionId"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("script.google.open", doc["payload"]["actionId"].as<const char*>());
  deserializeJson(doc, codec.encodeShortcutExecuteRequest("exec", 'g'));
  TEST_ASSERT_EQUAL_STRING("g", doc["payload"]["key"].as<const char*>());
  deserializeJson(doc, fixture("v2/script_page_first.json"));
  doc["execId"] = std::string(128, '"');
  int i = 0;
  for (JsonObject item : doc["result"]["data"]["actions"].as<JsonArray>()) {
    item["name"] = std::string(64, '\\');
    item["actionId"] = "script." + std::to_string(i++) + std::string(56, 'x');
  }
  TEST_ASSERT_LESS_OR_EQUAL(4096, compact(doc).size());
  TEST_ASSERT_TRUE(codec.decode(compact(doc)).ok);
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
  auto hello = codec.decode(fixture("v2/hello.json"));
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
  auto hello = codec.decode(fixture("v2/hello.json"));
  hello.message.protocolVersion = 1;
  adv::ConnectionSession session;
  session.onBleConnected();
  TEST_ASSERT_FALSE(session.acceptHello(hello.message));
}

void test_v2_shared_contract_and_tristates() {
  adv::MessageCodec codec;
  for (const auto* name : {"hello", "script_page_first", "script_page_last", "script_page_empty",
      "clipboard_page_first", "clipboard_page_last", "clipboard_page_empty", "script_execute_success",
      "script_execute_error", "script_shortcut_success", "clipboard_execute_success", "clipboard_shortcut_success",
      "clipboard_partial_failure", "clipboard_write_failed", "clipboard_unconfirmed", "busy", "not_found",
      "script_list_request", "clipboard_list_request", "script_execute_request", "clipboard_execute_request", "shortcut_request"}) {
    const auto result = codec.decode(fixture((std::string("v2/") + name + ".json").c_str()));
    TEST_ASSERT_TRUE_MESSAGE(result.ok, name);
  }
  for (const auto* name : {"hello_v1_rejected", "invalid_page", "invalid_clipboard_success", "invalid_list_request"})
    TEST_ASSERT_FALSE(codec.decode(fixture((std::string("v2/") + name + ".json").c_str())).ok);
  auto partial = codec.decode(fixture("v2/clipboard_partial_failure.json")).message;
  TEST_ASSERT_TRUE(partial.clipboardWritten == adv::NullableBool::kTrue);
  TEST_ASSERT_TRUE(partial.pasteSent == adv::NullableBool::kFalse);
  auto unknown = codec.decode(fixture("v2/clipboard_unconfirmed.json")).message;
  TEST_ASSERT_TRUE(unknown.pasteSent == adv::NullableBool::kUnknown);
  for (const auto* field : {"type", "actionId", "name", "exitCode", "clipboardWritten", "pasteSent", "reason"}) {
    JsonDocument doc; deserializeJson(doc, fixture("v2/clipboard_execute_success.json"));
    doc["result"]["data"].remove(field);
    TEST_ASSERT_FALSE_MESSAGE(codec.decode(compact(doc)).ok, field);
  }
  for (int kind = 0; kind < 12; ++kind) {
    JsonDocument doc; deserializeJson(doc, fixture("v2/clipboard_execute_success.json"));
    auto d = doc["result"]["data"];
    if (kind == 0) d["clipboardWritten"] = 1;
    if (kind == 1) d["pasteSent"] = "true";
    if (kind == 2) d["clipboardWritten"] = false;
    if (kind == 3) d["pasteSent"] = nullptr;
    if (kind == 4) d["type"] = "script";
    if (kind == 5) d["type"] = "future";
    if (kind == 6) d["reason"] = "WRITE_FAILED";
    if (kind == 7) d["actionId"] = "script.test";
    if (kind == 8) d["name"] = std::string("bad") + char(0) + "suffix";
    if (kind == 9) d["name"] = std::string(65, 'x');
    if (kind == 10) d["name"] = std::string("\xED\xA0\x80");
    if (kind == 11) d["content"] = "forbidden";
    TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  }
  for (int kind = 0; kind < 5; ++kind) {
    JsonDocument doc; deserializeJson(doc, fixture("v2/hello.json"));
    auto d = doc["result"]["data"];
    if (kind == 0) d.remove("supportedActionTypes");
    if (kind == 1) d["supportedActionTypes"] = nullptr;
    if (kind == 2) d["supportedActionTypes"][0] = "future";
    if (kind == 3) d["supportedActionTypes"][1] = d["supportedActionTypes"][0];
    if (kind == 4) d["supportedActionTypes"][0] = std::string("script") + char(0);
    TEST_ASSERT_FALSE(codec.decode(compact(doc)).ok);
  }
  JsonDocument doc; deserializeJson(doc, codec.encodeActionsListRequest("c", adv::ActionType::kClipboard, 16));
  TEST_ASSERT_EQUAL_STRING("clipboard", doc["payload"]["type"].as<const char*>());
  TEST_ASSERT_EQUAL(16, doc["payload"]["offset"].as<int>());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_v2_shared_contract_and_tristates);
  RUN_TEST(test_shared_fixtures_decode);
  RUN_TEST(test_hello_without_settings);
  RUN_TEST(test_time_contract);
  RUN_TEST(test_script_contract);
  RUN_TEST(test_script_metadata_does_not_truncate_embedded_nul);
  RUN_TEST(test_script_requests_and_maximum_page);
  RUN_TEST(test_missing_and_invalid_fields_rejected);
  RUN_TEST(test_request_encoding_and_exec_id);
  RUN_TEST(test_router_handles_known_only);
  RUN_TEST(test_session_requires_compatible_hello_and_clears);
  RUN_TEST(test_incompatible_version_rejected);
  return UNITY_END();
}
