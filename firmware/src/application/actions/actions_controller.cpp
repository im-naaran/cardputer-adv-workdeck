#include "application/actions/actions_controller.h"
#include <algorithm>
#include "core/protocol_constants.h"
#include "platform/monotonic_clock.h"

namespace adv {
void ActionsController::onSessionReady(const std::string& computer, bool list, bool execute,
    bool shortcut, const std::vector<ActionType>& types) {
  if (ready_ && computer_ == computer && listSupported_ == list && executeSupported_ == execute &&
      shortcutSupported_ == shortcut && types_ == types) return;
  disconnect();
  ready_ = true; computer_ = computer; listSupported_ = list;
  executeSupported_ = execute; shortcutSupported_ = shortcut; types_ = types;
  for (auto type : {ActionType::kScript, ActionType::kClipboard})
    directory(type).directory = list && supports(type) ? ActionDirectoryStatus::kNotLoaded : ActionDirectoryStatus::kUnsupported;
}
bool ActionsController::supports(ActionType type) const {
  return std::find(types_.begin(), types_.end(), type) != types_.end();
}
void ActionsController::enter(ActionType type, uint32_t now) {
  tick(now);
  if (directory(type).directory == ActionDirectoryStatus::kNotLoaded) requestPage(type, 0, false, now);
}
void ActionsController::disconnect() {
  for (auto& state : directories_) if (!state.directoryId.empty()) cancel_(state.directoryId);
  if (!executionId_.empty()) cancel_(executionId_);
  directories_ = {}; execution_ = {};
  executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
  computer_.clear(); types_.clear();
  ready_ = listSupported_ = executeSupported_ = shortcutSupported_ = false;
}
void ActionsController::failDirectory(ActionType type, const std::string& error) {
  auto& state_ = directory(type);
  if (!state_.directoryId.empty()) cancel_(state_.directoryId);
  state_.directoryId.clear();
  state_.directory = ActionDirectoryStatus::kFailed;
  state_.directoryError = error;
}
bool ActionsController::requestPage(ActionType type, uint64_t offset, bool selectLast, uint32_t now) {
  auto& state_ = directory(type);
  if (!ready_ || !listSupported_ || !supports(type) || !state_.directoryId.empty()) return false;
  state_.targetOffset = offset; state_.selectLast = selectLast;
  std::string id;
  if (!ids_.next(id) || !send_(id, codec_.encodeActionsListRequest(id, type, offset), now)) {
    failDirectory(type, "目录请求未发送"); return false;
  }
  state_.directoryId = id; state_.started = now;
  state_.directory = ActionDirectoryStatus::kLoading;
  state_.directoryError.clear();
  return true;
}
void ActionsController::moveSelection(ActionType type, int direction, uint32_t now) {
  auto& state_ = directory(type);
  if (state_.directory != ActionDirectoryStatus::kReady || state_.entries.empty()) return;
  if (direction < 0) {
    if (state_.selected > 0) --state_.selected;
    else if (state_.offset > 0) requestPage(type, state_.offset - protocol::kActionPageSize, true, now);
  } else if (direction > 0) {
    if (state_.selected + 1 < state_.entries.size()) ++state_.selected;
    else if (state_.hasNextOffset) requestPage(type, state_.nextOffset, false, now);
  }
}
bool ActionsController::confirm(ActionType type, uint32_t now) {
  auto& state_ = directory(type);
  // A failed page retains its old cache until a valid replacement arrives. Enter retries the target,
  // and a second explicit Enter is required to execute after the new page arrives.
  if (state_.directory == ActionDirectoryStatus::kFailed) return requestPage(type, state_.targetOffset, state_.selectLast, now);
  if (state_.directory != ActionDirectoryStatus::kReady || state_.entries.empty()) return false;
  return executeById(type, state_.entries[state_.selected].actionId, now);
}
void ActionsController::feedback(const std::string& text, uint32_t now) {
  execution_.feedback = text; execution_.feedbackAt = now;
}
bool ActionsController::executeById(ActionType type, const std::string& actionId, uint32_t now) {
  auto& state_ = directory(type);
  // Only IDs in the active page can originate a list execution; no stale-page Enter.
  if (state_.directory != ActionDirectoryStatus::kReady ||
      std::none_of(state_.entries.begin(), state_.entries.end(),
                   [&](const ActionEntry& entry) { return entry.actionId == actionId; })) return false;
  const bool sent = submitExecution(protocol::kActionsExecuteAction, actionId, 0, now);
  if (sent) executionType_ = type;
  return sent;
}
bool ActionsController::executeByKey(char key, uint32_t now) {
  if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
  if (key < 'a' || key > 'z') return false;
  // The desktop matches against the entire configuration; this never consults the page.
  return submitExecution(protocol::kShortcutExecuteAction, {}, key, now);
}
bool ActionsController::submitExecution(const std::string& action, const std::string& target,
                                         char key, uint32_t now) {
  tick(now);
  if (!executionId_.empty()) { feedback("动作执行中，请稍候", now); return false; }
  if (!ready_ || (key ? !shortcutSupported_ : !executeSupported_)) {
    feedback("动作执行不可用", now); return false;
  }
  std::string id;
  if (!ids_.next(id) || !send_(id, key ? codec_.encodeShortcutExecuteRequest(id, key)
                                    : codec_.encodeActionExecuteRequest(id, target), now)) {
    execution_.status = ActionExecutionStatus::kFailed;
    feedback("执行请求未发送", now); return false;
  }
  executionId_ = id; executionAction_ = action; executionTarget_ = target;
  executionStarted_ = now; execution_.status = ActionExecutionStatus::kRunning;
  feedback("动作执行中", now);
  return true;
}
bool ActionsController::validPage(ActionType type, const Message& m) const {
  auto& state_ = directory(type);
  // Codec validates each entry and wire types. Check request/snapshot consistency
  // before replacing the cache, including bounded sizes for direct native callers.
  if (!m.hasActionType || m.actionType != type || m.offset != state_.targetOffset || m.offset % protocol::kActionPageSize ||
      (state_.hasTotal && m.total != state_.total) ||
      (m.total == 0 ? m.offset != 0 : m.offset >= m.total)) return false;
  const auto remaining = m.total - m.offset;
  const auto count = std::min<uint64_t>(protocol::kActionPageSize, remaining);
  if (m.actions.size() != count || m.hasNextOffset != (remaining > count)) return false;
  return !m.hasNextOffset || m.nextOffset == m.offset + count;
}
bool ActionsController::onMessage(const Message& m, uint32_t now) {
  if (!ready_ || m.event != MessageEvent::kResponse) return false;
  // Receive processing can precede the loop's tick; an expired response must not
  // turn an unconfirmed execution into success just because it was dequeued first.
  tick(now);
  // Each directory owns its correlation; responses may arrive in either order.
  for (auto type : {ActionType::kScript, ActionType::kClipboard}) {
    auto& state_ = directory(type);
    if (state_.directoryId.empty() || m.actionId != protocol::kActionsListAction || m.execId != state_.directoryId) continue;
    if (m.resultCode != "OK" || !validPage(type, m)) { failDirectory(type, "目录加载失败"); return true; }
    state_.entries = m.actions; state_.offset = m.offset; state_.total = m.total;
    state_.hasNextOffset = m.hasNextOffset; state_.nextOffset = m.nextOffset;
    state_.selected = state_.selectLast && !m.actions.empty() ? m.actions.size() - 1 : 0;
    state_.hasTotal = true; state_.directoryId.clear();
    state_.directory = ActionDirectoryStatus::kReady; state_.directoryError.clear();
    return true;
  }
  if (executionId_.empty() || m.actionId != executionAction_ || m.execId != executionId_) return false;
  // Errors before matching may have null data. A supplied identity must still agree.
  if (!executionTarget_.empty() && !m.executedActionId.empty() && m.executedActionId != executionTarget_) return false;
  if (m.hasActionType && (!executionTarget_.empty() && m.actionType != executionType_)) return false;
  if (m.resultCode == "OK") {
    if (!m.hasActionType || !supports(m.actionType) || m.executedActionId.empty() || m.executedName.empty()) return false;
    if (m.actionType == ActionType::kScript ? (!m.hasExitCode || m.exitCode != 0) :
        (m.hasExitCode || m.clipboardWritten != NullableBool::kTrue || m.pasteSent != NullableBool::kTrue)) return false;
  }
  executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
  if (m.resultCode == "OK") {
    execution_.status = ActionExecutionStatus::kSucceeded;
    // Keep the outcome visible even when a long name needs display truncation.
    feedback(std::string(m.actionType == ActionType::kClipboard ? "已发送粘贴：" : "已执行：") + m.executedName, now);
  } else if (m.resultCode == "TIMEOUT" || m.reason == "UNCONFIRMED") {
    // Timeout cannot establish whether external side effects already happened.
    execution_.status = ActionExecutionStatus::kUnconfirmed;
    feedback("执行超时，结果未确认", now);
  } else {
    execution_.status = ActionExecutionStatus::kFailed;
    if (m.hasActionType && m.actionType == ActionType::kClipboard && m.clipboardWritten == NullableBool::kTrue) {
      feedback("粘贴失败，文本已复制", now); return true;
    }
    feedback(m.resultCode == "BUSY" ? "电脑动作忙碌，请稍候" :
             m.resultMessage.empty() ? "动作执行失败" : m.resultMessage, now);
  }
  return true;
}
void ActionsController::tick(uint32_t now) {
  // Results and local errors expire once; running feedback persists across pages.
  if (executionId_.empty() && !execution_.feedback.empty() && elapsedMs(now, execution_.feedbackAt) >= 3000)
    execution_.feedback.clear();
  for (auto type : {ActionType::kScript, ActionType::kClipboard}) {
    const auto& state = directory(type);
    if (!state.directoryId.empty() && elapsedMs(now, state.started) >= 15000) failDirectory(type, "目录加载超时");
  }
  if (!executionId_.empty() && elapsedMs(now, executionStarted_) >= 45000) {
    // Only queued work can be cancelled; never replay a possibly executed action.
    cancel_(executionId_);
    executionId_.clear(); executionAction_.clear(); executionTarget_.clear();
    execution_.status = ActionExecutionStatus::kUnconfirmed;
    feedback("结果未确认，请检查电脑", now);
  }
}
}  // namespace adv
