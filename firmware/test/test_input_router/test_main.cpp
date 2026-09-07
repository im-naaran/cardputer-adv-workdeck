#include <unity.h>
#include "core/input_router.h"
using namespace adv;
void edges() {
  KeyPressTracker tracker;
  InputSnapshot s;
  s.alt=true; TEST_ASSERT_TRUE(tracker.update(s).empty());
  s.pressed['g']=true; auto events=tracker.update(s);
  TEST_ASSERT_EQUAL(1,events.size()); TEST_ASSERT_TRUE(events[0].alt);
  TEST_ASSERT_TRUE(tracker.update(s).empty());
  s.fn=true; s.alt=false; TEST_ASSERT_TRUE(tracker.update(s).empty());
  s.pressed['g']=false; s.pressed['h']=true;
  events=tracker.update(s); TEST_ASSERT_EQUAL(1,events.size()); TEST_ASSERT_EQUAL('h',events[0].character);
  s={}; tracker.update(s); s.pressed['g']=true;
  events=tracker.update(s); TEST_ASSERT_FALSE(events[0].alt);
  s.alt=true; TEST_ASSERT_TRUE(tracker.update(s).empty());
  s.pressed['g']=false; tracker.update(s); s.pressed['g']=true;
  TEST_ASSERT_TRUE(tracker.update(s)[0].alt);
  s={}; tracker.update(s); s.pressed['g']=true; s.pressed['h']=true;
  TEST_ASSERT_TRUE(tracker.update(s).empty());
  s.pressed['h']=false; TEST_ASSERT_TRUE(tracker.update(s).empty());
  s={}; tracker.update(s); s.fn=true; s.pressed['\r']=true;
  events=tracker.update(s); TEST_ASSERT_EQUAL_INT((int)Key::kEnter,(int)events[0].key); TEST_ASSERT_TRUE(events[0].fn);
}
void routing_matrix() {
  InputRouter router;
  for(int m=0;m<4;++m) {
    auto module=static_cast<Module>(m);
    for(unsigned mask=0;mask<32;++mask) {
      KeyEvent e{Key::kCharacter,(mask&1)!=0,'g',(mask&2)!=0,(mask&4)!=0,(mask&8)!=0,(mask&16)!=0};
      auto result=router.route(e,module);
      auto expected=mask==0?InputAction::kPageKey:mask==2?InputAction::kShortcut:InputAction::kConsumed;
      TEST_ASSERT_EQUAL_INT((int)expected,(int)result.action);
      e.key=Key::kEnter; e.character='\r';
      expected=mask==0?InputAction::kConfirm:mask==1&&m==0?InputAction::kCodexToggle:InputAction::kConsumed;
      TEST_ASSERT_EQUAL_INT((int)expected,(int)router.route(e,module).action);
      const char punctuation[]={';',',','.','/'};
      const Key directions[]={Key::kUp,Key::kLeft,Key::kDown,Key::kRight};
      for(int p=0;p<4;++p) {
        e.key=Key::kCharacter; e.character=punctuation[p];
        expected=mask==0?InputAction::kDirection:((mask==1||mask==3)&&(p==1||p==3))?InputAction::kNavigation:InputAction::kConsumed;
        auto r=router.route(e,module);
        TEST_ASSERT_EQUAL_INT((int)expected,(int)r.action);
        if(expected==InputAction::kDirection || expected==InputAction::kNavigation)
          TEST_ASSERT_EQUAL_INT((int)directions[p],(int)r.event.key);
      }
      for(Key digit:{Key::kDigit1,Key::kDigit2,Key::kDigit3,Key::kDigit4}) {
        e.key=digit; e.character=0;
        expected=mask==0?InputAction::kPageKey:(mask==1||mask==3)?InputAction::kNavigation:InputAction::kConsumed;
        TEST_ASSERT_EQUAL_INT((int)expected,(int)router.route(e,module).action);
      }
    }
    InputConfig config; config.directionMapping[m]=false; router.applyConfig(config);
    for(int other=0;other<4;++other) {
      auto expected=other==m?InputAction::kPageKey:InputAction::kDirection;
      TEST_ASSERT_EQUAL_INT((int)expected,(int)router.route({Key::kCharacter,false,';'},static_cast<Module>(other)).action);
    }
    router.applyConfig({});
  }
  TEST_ASSERT_EQUAL_INT((int)InputAction::kConsumed,(int)router.route({Key::kCharacter,true,'5'},Module::kCodex).action);
  TEST_ASSERT_EQUAL('g',router.route({Key::kCharacter,false,'G',true},Module::kSettings).event.character);
}
void text_editing() {
  InputRouter router;
  KeyPressTracker tracker;
  // A miniature hardware map exercises the same two-layer snapshot used by the adapter.
  for (auto pair : {std::pair<char,char>{'a','A'}, {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'},
      {';',':'}, {',','<'}, {'.','>'}, {'/','?'}, {' ',' '}}) {
    for (bool shift : {false,true}) {
      tracker.update({});
      InputSnapshot snapshot;
      snapshot.pressed[static_cast<unsigned char>(pair.first)]=true;
      snapshot.shifted[static_cast<unsigned char>(pair.first)]=pair.second;
      snapshot.shift=shift;
      auto event=tracker.update(snapshot).at(0);
      TEST_ASSERT_EQUAL(pair.first,event.character);
      TEST_ASSERT_EQUAL(shift?pair.second:pair.first,event.text);
      auto result=router.route(event,Module::kSettings,true);
      TEST_ASSERT_TRUE(result.action==InputAction::kPageKey);
      TEST_ASSERT_EQUAL(event.text,result.event.text);
      TEST_ASSERT_TRUE(tracker.update(snapshot).empty());
      // Changing just Shift while holding a key must not create another character.
      snapshot.shift=!shift;
      TEST_ASSERT_TRUE(tracker.update(snapshot).empty());
    }
  }
  for(char control : {'\r','\b','\t'}) {
    tracker.update({}); InputSnapshot snapshot; snapshot.pressed[control]=true;
    auto event=tracker.update(snapshot).at(0);
    const auto expected=control=='\r'?Key::kEnter:control=='\b'?Key::kBackspace:Key::kTab;
    TEST_ASSERT_TRUE(event.key==expected);
    TEST_ASSERT_TRUE(router.route(event,Module::kSettings,true).action==
        (control=='\r'?InputAction::kConfirm:InputAction::kPageKey));
  }
  KeyEvent letter{Key::kCharacter,false,'g',true,false,false,false,'g'};
  TEST_ASSERT_TRUE(router.route(letter,Module::kSettings,true).action==InputAction::kShortcut);
  letter.shift=true;
  TEST_ASSERT_TRUE(router.route(letter,Module::kSettings,true).action==InputAction::kConsumed);
  letter.alt=false; letter.ctrl=true;
  TEST_ASSERT_TRUE(router.route(letter,Module::kSettings,true).action==InputAction::kConsumed);
  letter.ctrl=false; letter.opt=true;
  TEST_ASSERT_TRUE(router.route(letter,Module::kSettings,true).action==InputAction::kConsumed);
  TEST_ASSERT_TRUE(router.route({Key::kDigit4,true,'4'},Module::kSettings,true).action==InputAction::kNavigation);
  TEST_ASSERT_TRUE(router.route({Key::kCharacter,true,','},Module::kSettings,true).action==InputAction::kNavigation);
  TEST_ASSERT_TRUE(router.route({Key::kEnter,true,'\r'},Module::kSettings,true).action==InputAction::kConsumed);
  TEST_ASSERT_TRUE(router.route({Key::kEnter,true,'\r'},Module::kCodex,true).action==InputAction::kCodexToggle);
  InputConfig config; config.directionMapping[3]=false; router.applyConfig(config);
  TEST_ASSERT_TRUE(router.route({Key::kCharacter,false,';',false,false,false,false,';'},Module::kSettings,true).action==InputAction::kPageKey);
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(edges); RUN_TEST(routing_matrix); RUN_TEST(text_editing); return UNITY_END(); }
