#include <unity.h>
#include "core/connection_session.h"
#include "core/protocol_constants.h"

void hello_changes_and_invalidation() {
  adv::ConnectionSession session;
  adv::Message m;
  m.actionId = adv::protocol::kHelloAction;
  m.resultCode = "OK";
  m.protocolVersion = adv::protocol::kVersion;
  m.computerId = "first";
  m.capabilities = {adv::protocol::kCodexUsageAction};
  m.supportedActionTypes = {adv::ActionType::kScript, adv::ActionType::kClipboard};
  TEST_ASSERT_FALSE(session.acceptHello(m));
  session.onBleConnected();
  TEST_ASSERT_FALSE(session.ready());
  TEST_ASSERT_TRUE(session.acceptHello(m));
  TEST_ASSERT_TRUE(session.acceptHello(m));
  TEST_ASSERT_EQUAL_STRING("first", session.computerId().c_str());
  TEST_ASSERT_TRUE(session.supportsActionType(adv::ActionType::kClipboard));
  m.supportedActionTypes = {adv::ActionType::kScript};
  m.computerId = "second";
  m.capabilities = {adv::protocol::kTimeReadAction};
  TEST_ASSERT_TRUE(session.acceptHello(m));
  TEST_ASSERT_EQUAL_STRING("second", session.computerId().c_str());
  TEST_ASSERT_FALSE(session.supports(adv::protocol::kCodexUsageAction));
  TEST_ASSERT_TRUE(session.supports(adv::protocol::kTimeReadAction));
  TEST_ASSERT_FALSE(session.supportsActionType(adv::ActionType::kClipboard));
  m.protocolVersion = 1;
  TEST_ASSERT_FALSE(session.acceptHello(m));
  TEST_ASSERT_FALSE(session.ready());
  TEST_ASSERT_TRUE(session.bleConnected());
  TEST_ASSERT_TRUE(session.computerId().empty());
  TEST_ASSERT_FALSE(session.supportsActionType(adv::ActionType::kScript));
  TEST_ASSERT_FALSE(session.supports(adv::protocol::kTimeReadAction));
  m.protocolVersion = adv::protocol::kVersion;
  TEST_ASSERT_TRUE(session.acceptHello(m));
  session.onBleConnected();
  TEST_ASSERT_FALSE(session.ready());
  TEST_ASSERT_TRUE(session.computerId().empty());
  TEST_ASSERT_FALSE(session.supportsActionType(adv::ActionType::kScript));
}
int main(int, char**) {
  UNITY_BEGIN(); RUN_TEST(hello_changes_and_invalidation); return UNITY_END();
}
