#include "application/scripts/scripts_controller.h"
#include <algorithm>
#include "core/protocol_constants.h"
#include "platform/monotonic_clock.h"

namespace adv {
void ScriptsController::onSessionReady(const std::string& computer, bool list, bool execute,
                                       bool shortcut, uint32_t now) {
  if (ready_ && computer_ == computer && listSupported_ == list &&
      executeSupported_ == execute && shortcutSupported_ == shortcut) return;
  // Capability/identity renegotiation invalidates the snapshot and all correlations.
  disconnect();
  ready_ = true;
  computer_ = computer;
  listSupported_ = list;
  executeSupported_ = execute;
  shortcutSupported_ = shortcut;
  if (list) requestPage(0, false, now);
}
void ScriptsController::disconnect() {
  if (!directoryId_.empty()) cancel_(directoryId_);
  if (!executionId_.empty()) cancel_(executionId_);
  directoryId_.clear(); executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
  computer_.clear();
  ready_ = listSupported_ = executeSupported_ = shortcutSupported_ = hasTotal_ = false;
  targetOffset_ = 0; selectLast_ = false;
  state_ = {};
}
void ScriptsController::failDirectory(const std::string& error) {
  if (!directoryId_.empty()) cancel_(directoryId_);
  directoryId_.clear();
  state_.directory = ScriptsDirectoryStatus::kFailed;
  state_.directoryError = error;
}
bool ScriptsController::requestPage(uint64_t offset, bool selectLast, uint32_t now) {
  if (!ready_ || !listSupported_ || !directoryId_.empty()) return false;
  targetOffset_ = offset; selectLast_ = selectLast;
  std::string id;
  if (!ids_.next(id) || !send_(id, codec_.encodeActionsListRequest(id, offset), now)) {
    failDirectory("目录请求未发送"); return false;
  }
  directoryId_ = id; directoryStarted_ = now;
  state_.directory = ScriptsDirectoryStatus::kLoading;
  state_.directoryError.clear();
  return true;
}
void ScriptsController::moveSelection(int direction, uint32_t now) {
  if (state_.directory != ScriptsDirectoryStatus::kReady || state_.entries.empty()) return;
  if (direction < 0) {
    if (state_.selected > 0) --state_.selected;
    else if (state_.offset > 0) requestPage(state_.offset - protocol::kScriptPageSize, true, now);
  } else if (direction > 0) {
    if (state_.selected + 1 < state_.entries.size()) ++state_.selected;
    else if (state_.hasNextOffset) requestPage(state_.nextOffset, false, now);
  }
}
bool ScriptsController::confirm(uint32_t now) {
  // A failed page retains its old cache until a valid replacement arrives. Enter retries the target,
  // and a second explicit Enter is required to execute after the new page arrives.
  if (state_.directory == ScriptsDirectoryStatus::kFailed) return requestPage(targetOffset_, selectLast_, now);
  if (state_.directory != ScriptsDirectoryStatus::kReady || state_.entries.empty()) return false;
  return executeById(state_.entries[state_.selected].actionId, now);
}
void ScriptsController::feedback(const std::string& text, uint32_t now) {
  state_.feedback = text; state_.feedbackAt = now;
}
bool ScriptsController::executeById(const std::string& actionId, uint32_t now) {
  // Only IDs in the active page can originate a list execution; no stale-page Enter.
  if (state_.directory != ScriptsDirectoryStatus::kReady ||
      std::none_of(state_.entries.begin(), state_.entries.end(),
                   [&](const ScriptEntry& entry) { return entry.actionId == actionId; })) return false;
  return submitExecution(protocol::kScriptsExecuteAction, actionId, 0, now);
}
bool ScriptsController::executeByKey(char key, uint32_t now) {
  if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
  if (key < 'a' || key > 'z') return false;
  // The desktop matches against the entire configuration; this never consults the page.
  return submitExecution(protocol::kShortcutExecuteAction, {}, key, now);
}
bool ScriptsController::submitExecution(const std::string& action, const std::string& target,
                                         char key, uint32_t now) {
  if (!executionId_.empty()) { feedback("脚本执行中，请稍候", now); return false; }
  if (!ready_ || (key ? !shortcutSupported_ : !executeSupported_)) {
    feedback("脚本执行不可用", now); return false;
  }
  std::string id;
  if (!ids_.next(id) || !send_(id, key ? codec_.encodeShortcutExecuteRequest(id, key)
                                    : codec_.encodeScriptExecuteRequest(id, target), now)) {
    state_.execution = ScriptExecutionStatus::kFailed;
    feedback("执行请求未发送", now); return false;
  }
  executionId_ = id; executionAction_ = action; executionTarget_ = target;
  executionStarted_ = now; state_.execution = ScriptExecutionStatus::kRunning;
  feedback("脚本执行中", now);
  return true;
}
bool ScriptsController::validPage(const Message& m) const {
  // Codec validates each entry and wire types. Check request/snapshot consistency
  // before replacing the cache, including bounded sizes for direct native callers.
  if (m.offset != targetOffset_ || m.offset % protocol::kScriptPageSize ||
      (hasTotal_ && m.total != state_.total) ||
      (m.total == 0 ? m.offset != 0 : m.offset >= m.total)) return false;
  const auto remaining = m.total - m.offset;
  const auto count = std::min<uint64_t>(protocol::kScriptPageSize, remaining);
  if (m.scripts.size() != count || m.hasNextOffset != (remaining > count)) return false;
  return !m.hasNextOffset || m.nextOffset == m.offset + count;
}
bool ScriptsController::onMessage(const Message& m, uint32_t now) {
  if (!ready_ || m.event != MessageEvent::kResponse) return false;
  // Receive processing can precede the loop's tick; an expired response must not
  // turn an unconfirmed execution into success just because it was dequeued first.
  tick(now);
  if (!directoryId_.empty() && m.actionId == protocol::kActionsListAction && m.execId == directoryId_) {
    if (m.resultCode != "OK" || !validPage(m)) { failDirectory("目录加载失败"); return true; }
    state_.entries = m.scripts;
    state_.offset = m.offset; state_.total = m.total;
    state_.hasNextOffset = m.hasNextOffset; state_.nextOffset = m.nextOffset;
    state_.selected = selectLast_ && !m.scripts.empty() ? m.scripts.size() - 1 : 0;
    hasTotal_ = true; directoryId_.clear();
    state_.directory = ScriptsDirectoryStatus::kReady; state_.directoryError.clear();
    return true;
  }
  if (executionId_.empty() || m.actionId != executionAction_ || m.execId != executionId_) return false;
  // Errors before matching may have null data. A supplied identity must still agree.
  if (!executionTarget_.empty() && !m.executedActionId.empty() && m.executedActionId != executionTarget_) return false;
  if (m.resultCode == "OK" && (m.executedActionId.empty() || m.executedName.empty() || !m.hasExitCode || m.exitCode != 0)) return false;
  executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
  if (m.resultCode == "OK") {
    state_.execution = ScriptExecutionStatus::kSucceeded;
    // Keep the outcome visible even when a long name needs display truncation.
    feedback("已执行：" + m.executedName, now);
  } else if (m.resultCode == "TIMEOUT") {
    // Timeout cannot establish whether external side effects already happened.
    state_.execution = ScriptExecutionStatus::kUnconfirmed;
    feedback("执行超时，结果未确认", now);
  } else {
    state_.execution = ScriptExecutionStatus::kFailed;
    feedback(m.resultCode == "BUSY" ? "电脑脚本忙碌，请稍候" :
             m.resultMessage.empty() ? "脚本执行失败" : m.resultMessage, now);
  }
  return true;
}
void ScriptsController::tick(uint32_t now) {
  // Results and local errors expire once; running feedback persists across pages.
  if (executionId_.empty() && !state_.feedback.empty() && elapsedMs(now, state_.feedbackAt) >= 3000)
    state_.feedback.clear();
  if (!directoryId_.empty() && elapsedMs(now, directoryStarted_) >= 15000) failDirectory("目录加载超时");
  if (!executionId_.empty() && elapsedMs(now, executionStarted_) >= 45000) {
    // Only queued work can be cancelled; never replay a possibly executed action.
    cancel_(executionId_);
    executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
    state_.execution = ScriptExecutionStatus::kUnconfirmed;
    feedback("结果未确认，请检查电脑", now);
  }
}
}  // namespace adv
