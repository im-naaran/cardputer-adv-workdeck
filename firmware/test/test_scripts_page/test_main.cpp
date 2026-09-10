#include <unity.h>
#include "application/actions/action_list_page.h"
using namespace adv;
void scrolling_and_shortcuts() {
  DisplayAdapter display; ActionListPage page(ActionType::kScript); ActionDirectoryState state;
  state.directory=ActionDirectoryStatus::kReady;state.total=17;
  for(int i=0;i<8;++i)state.entries.push_back({"script."+std::to_string(i),"打开测试脚本"+std::to_string(i),"g",i==0?"g":""});
  auto view=page.makeView(display,state);
  TEST_ASSERT_EQUAL(4,view.rows.size());TEST_ASSERT_EQUAL_STRING("Alt+G",view.rows[0].shortcut.c_str());
  TEST_ASSERT_TRUE(view.rows[1].shortcut.empty());TEST_ASSERT_TRUE(view.rows[0].selected);
  state.selected=6;view=page.makeView(display,state);
  TEST_ASSERT_EQUAL(3,view.scrollOffset);TEST_ASSERT_TRUE(view.rows[3].selected);
  state.selected=7;view=page.makeView(display,state);TEST_ASSERT_EQUAL(4,view.scrollOffset);
  state.offset=8;state.selected=0;view=page.makeView(display,state);
  TEST_ASSERT_EQUAL(0,view.scrollOffset);TEST_ASSERT_EQUAL_STRING("9/17  Enter 执行",view.footer.c_str());
  state.offset=0;state.selected=7;view=page.makeView(display,state);
  TEST_ASSERT_EQUAL(4,view.scrollOffset);TEST_ASSERT_TRUE(view.rows[3].selected);
}
void states_hide_old_rows() {
  DisplayAdapter display;ActionListPage page(ActionType::kScript);ActionDirectoryState state;
  state.entries.push_back({"script.old","旧页","",""});
  for(auto status:{ActionDirectoryStatus::kLoading,ActionDirectoryStatus::kFailed,ActionDirectoryStatus::kUnsupported}) {
    state.directory=status;auto view=page.makeView(display,state);
    TEST_ASSERT_TRUE(view.rows.empty());TEST_ASSERT_FALSE(view.message.empty());
    if(status==ActionDirectoryStatus::kFailed)TEST_ASSERT_TRUE(view.hint.find("Enter")!=std::string::npos);
  }
  state.directory=ActionDirectoryStatus::kFailed;
  state.directoryError="目录请求未发送";
  TEST_ASSERT_EQUAL_STRING("目录请求未发送",page.makeView(display,state).message.c_str());
  state.directoryError="目录加载超时";
  TEST_ASSERT_EQUAL_STRING("目录加载超时",page.makeView(display,state).message.c_str());
  state.directory=ActionDirectoryStatus::kReady;state.entries.clear();
  TEST_ASSERT_EQUAL_STRING("暂无启用脚本",page.makeView(display,state).message.c_str());
}
void utf8_fitting_keeps_action_identity() {
  DisplayAdapter display;ActionListPage page(ActionType::kScript);ActionDirectoryState state;state.directory=ActionDirectoryStatus::kReady;
  state.total=1;const std::string name="很长的中文脚本名字用于验证显示裁剪不会改变执行对象";
  state.entries.push_back({"script.identity",name,"g","g"});
  auto view=page.makeView(display,state);const auto title=view.rows[0].title;
  TEST_ASSERT_TRUE(display.textWidth(title,FontStyle::kChinese)<=178);
  TEST_ASSERT_EQUAL_STRING(name.c_str(),state.entries[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("script.identity",state.entries[0].actionId.c_str());
  const auto prefix=title.substr(0,title.size()-2);TEST_ASSERT_EQUAL(0,prefix.size()%3);
  TEST_ASSERT_TRUE(name.compare(0,prefix.size(),prefix)==0);
  TEST_ASSERT_EQUAL_STRING("中..",display.fitText("中文测试",40).c_str());
  TEST_ASSERT_EQUAL_STRING("短名",display.fitText("短名",32).c_str());
  TEST_ASSERT_TRUE(display.fitText("字",1).empty());TEST_ASSERT_TRUE(display.fitText("",-1).empty());
  TEST_ASSERT_EQUAL_STRING("A..",display.fitText("A😀B",24).c_str());
}
void clipboard_labels_and_independent_scroll() {
  DisplayAdapter display;
  ActionListPage script(ActionType::kScript), clipboard(ActionType::kClipboard);
  ActionDirectoryState state;
  TEST_ASSERT_EQUAL_STRING("剪贴板目录不可用",clipboard.makeView(display,state).message.c_str());
  state.directory=ActionDirectoryStatus::kNotLoaded;
  TEST_ASSERT_EQUAL_STRING("正在加载剪贴板",clipboard.makeView(display,state).message.c_str());
  state.directory=ActionDirectoryStatus::kFailed;
  TEST_ASSERT_EQUAL_STRING("剪贴板加载失败",clipboard.makeView(display,state).message.c_str());
  state.directory=ActionDirectoryStatus::kReady;
  TEST_ASSERT_EQUAL_STRING("暂无启用剪贴板",clipboard.makeView(display,state).message.c_str());
  state.total=17;
  for(int i=0;i<8;++i)state.entries.push_back({"clipboard.item"+std::to_string(i),"中文长名称用于验证剪贴板显示裁剪","g","g",ActionType::kClipboard});
  state.selected=7;
  auto view=clipboard.makeView(display,state);
  TEST_ASSERT_EQUAL(4,view.rows.size());TEST_ASSERT_EQUAL(4,view.scrollOffset);
  TEST_ASSERT_EQUAL_STRING("8/17  Enter 粘贴",view.footer.c_str());
  TEST_ASSERT_TRUE(display.textWidth(view.rows[0].title,FontStyle::kChinese)<=178);
  state.selected=0;TEST_ASSERT_EQUAL(0,script.makeView(display,state).scrollOffset);
  state.selected=7;TEST_ASSERT_EQUAL(4,clipboard.makeView(display,state).scrollOffset);
  clipboard.reset();state.selected=0;TEST_ASSERT_EQUAL(0,clipboard.makeView(display,state).scrollOffset);
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(clipboard_labels_and_independent_scroll);RUN_TEST(scrolling_and_shortcuts);RUN_TEST(states_hide_old_rows);RUN_TEST(utf8_fitting_keeps_action_identity);return UNITY_END();}
