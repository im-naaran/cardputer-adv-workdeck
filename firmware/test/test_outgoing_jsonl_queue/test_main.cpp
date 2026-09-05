#include <unity.h>
#include "core/outgoing_jsonl_queue.h"
void frames() {
  adv::OutgoingJsonlQueue q; std::string out;
  auto sink=[&](const std::string& c){out+=c; return true;};
  TEST_ASSERT_FALSE(q.enqueue("a","a\nb",0,1));
  TEST_ASSERT_FALSE(q.enqueue("a",std::string(4097,'x'),0,1));
  TEST_ASSERT_TRUE(q.enqueue("a",std::string(25,'x'),0,1));
  TEST_ASSERT_TRUE(q.enqueue("b","中文",0,1));
  q.poll(0,1,sink); TEST_ASSERT_EQUAL(20,out.size());
  TEST_ASSERT_FALSE(q.cancelPending("a"));
  q.poll(9,1,sink); TEST_ASSERT_EQUAL(20,out.size());
  q.poll(10,1,sink); q.poll(20,1,sink);
  TEST_ASSERT_EQUAL_STRING((std::string(25,'x')+"\n中文\n").c_str(),out.c_str());
  TEST_ASSERT_EQUAL(0,q.size());
}
void capacity_expiry_and_reset() {
  adv::OutgoingJsonlQueue q;
  for(int i=0;i<4;++i) TEST_ASSERT_TRUE(q.enqueue(std::to_string(i),"{}",0,1));
  TEST_ASSERT_FALSE(q.enqueue("full","{}",0,1));
  TEST_ASSERT_TRUE(q.cancelPending("1"));
  q.poll(10001,1,[](const std::string&){TEST_FAIL_MESSAGE("expired frame sent");return true;});
  TEST_ASSERT_EQUAL(0,q.size());
  q.enqueue("x","{}",0,1);
  q.poll(0,2,[](const std::string&){TEST_FAIL_MESSAGE("old generation sent");return true;});
  TEST_ASSERT_EQUAL(0,q.size());
  q.enqueue("x",std::string(30,'x'),0xfffffffe,2);
  int calls=0;
  q.poll(0xfffffffe,2,[&](const std::string&){++calls; return false;});
  q.poll(1,2,[&](const std::string&){++calls; return true;});
  TEST_ASSERT_EQUAL(1,calls);
  q.poll(8,2,[&](const std::string&){++calls; return true;});
  TEST_ASSERT_EQUAL(2,calls);
  q.clear(); TEST_ASSERT_EQUAL(0,q.size());
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(frames); RUN_TEST(capacity_expiry_and_reset); return UNITY_END(); }
