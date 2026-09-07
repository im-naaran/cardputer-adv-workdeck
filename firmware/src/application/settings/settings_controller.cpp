#include "application/settings/settings_controller.h"

namespace adv {
namespace {
std::string storageMessage(ConfigStatus status) {
  switch (status) {
    case ConfigStatus::kOk: return "已保存";
    case ConfigStatus::kNotFound: return "未配置，使用默认值";
    case ConfigStatus::kNotMounted: return "文件系统未挂载";
    case ConfigStatus::kReadFailed: return "配置读取失败";
    case ConfigStatus::kInvalidConfig: return "配置损坏或格式错误";
    case ConfigStatus::kWriteFailed: return "保存失败";
    case ConfigStatus::kReloadFailed: return "文件可能已保存，读取失败";
    case ConfigStatus::kApplyFailed: return "已保存但应用失败";
  }
  return {};
}
std::string outcomeText(WifiOutcome outcome) {
  switch (outcome) {
    case WifiOutcome::kSucceeded: return "成功";
    case WifiOutcome::kFailed: return "失败";
    case WifiOutcome::kTimedOut: return "超时";
    case WifiOutcome::kDisconnected: return "连接断开";
    case WifiOutcome::kCancelled: return "已取消";
    default: return "进行中";
  }
}
}
void SettingsController::loadBrightness() {
  const auto result = display_.reload();
  level_ = display_.saved().brightnessLevel;
  brightness_(level_ * 51);
  displayMessage_ = storageMessage(result.status);
}
void SettingsController::loadWifi() {
  // Loading credentials never acquires a wireless request or enables the radio.
  const auto result = wifiConfig_.reload();
  saved_ = wifiConfig_.saved();
  savedValid_ = saved_.valid();
  draft_ = saved_;
  wifiMessage_ = result.status == ConfigStatus::kNotFound ? "未配置" : storageMessage(result.status);
}
void SettingsController::refreshWifi() {
  const bool clean = draft_ == saved_;
  const auto result = wifiConfig_.reload();
  if (result.status == ConfigStatus::kOk) {
    saved_ = result.config;
    savedValid_ = true;
    if (clean) draft_ = saved_;
  } else wifiMessage_ = result.status == ConfigStatus::kNotFound ? "未配置" : storageMessage(result.status);
}
void SettingsController::refreshCodex() {
  savedCodex_ = config_.read();
  codexMessage_ = storageMessage(savedCodex_.status);
}
void SettingsController::setBrightness(int level) {
  if (level < 1 || level > 5) return;
  // Apply first; a failed persistence attempt must not undo visible feedback.
  if (level_ != level) { level_ = level; brightness_(level_ * 51); }
  const auto result = display_.save(encodeDisplayConfig({level_}));
  displayMessage_ = result.status == ConfigStatus::kOk ? "已保存" :
      "当前生效，" + storageMessage(result.status);
}
bool SettingsController::saveMinutes(const std::string& text) {
  unsigned minutes = 0;
  bool valid = !text.empty() && text.size() <= 2;
  for (char c : text) {
    if (c < '0' || c > '9') { valid = false; break; }
    minutes = minutes * 10 + c - '0';
  }
  if (!valid || minutes < 1 || minutes > 60) {
    codexMessage_ = "请输入1至60整数分钟";
    return false;
  }
  // Conversion only occurs on explicit save; 90-second legacy values stay exact.
  const auto result = config_.save(encodeCodexConfig({minutes * 60}));
  codexMessage_ = storageMessage(result.status);
  savedCodex_ = config_.read();
  return result.status == ConfigStatus::kOk;
}
void SettingsController::setField(size_t field, const std::string& value) {
  if (field > 2 || value.size() > (field == 0 ? 32u : 64u)) return;
  std::string* fields[] = {&draft_.ssid, &draft_.username, &draft_.password};
  if (*fields[field] == value) return;
  *fields[field] = value;
  wifiMessage_ = "草稿未保存";
}
bool SettingsController::validateWifi() {
  if (draft_.valid()) return true;
  if (draft_.ssid.empty()) wifiMessage_ = "SSID不能为空";
  else if (!WifiConfig{draft_.ssid, "", ""}.valid()) wifiMessage_ = "SSID格式错误或超过32字节";
  else if (!draft_.username.empty() && !WifiConfig{"x", draft_.username, "p"}.valid())
    wifiMessage_ = "用户名格式错误或超过64字节";
  else if (!draft_.username.empty() && draft_.password.empty()) wifiMessage_ = "公司网络密码不能为空";
  else if (draft_.username.empty()) wifiMessage_ = "密码需为空、8至63位ASCII或64位十六进制";
  else wifiMessage_ = "字段格式错误或超过64字节";
  return false;
}
bool SettingsController::saveWifi() {
  if (!validateWifi()) { wifiSaveMessage_ = wifiMessage_; return false; }
  const auto result = wifiConfig_.save(encodeWifiConfig(draft_));
  wifiMessage_ = storageMessage(result.status);
  wifiSaveMessage_ = wifiMessage_;
  if (result.status != ConfigStatus::kOk) return false;
  saved_ = result.config; savedValid_ = true;
  return true;
}
bool SettingsController::accept(WifiRequest request) {
  if (!request.requestId) {
    wifiMessage_ = request.availability == WifiAvailability::kReleaseFailed ?
        "Wi-Fi关闭失败，请重试关闭" : request.availability == WifiAvailability::kBusy ?
        "Wi-Fi忙，请稍后再试" : "无法启动Wi-Fi操作";
    return false;
  }
  requestId_ = request.requestId;
  observed_ = wifi_.status(requestId_);
  wifiMessage_ = observed_.operation == WifiOperation::kScan ? "扫描已启动" : "测试已启动，不自动保存";
  return true;
}
bool SettingsController::scan() {
  if (!accept(wifi_.scan())) return false;
  scans_ = {};
  return true;
}
bool SettingsController::test() {
  if (!validateWifi() || !accept(wifi_.test(draft_))) return false;
  // Compare all credentials, never just SSID; edits cannot alter an in-flight test.
  tested_ = draft_; haveTest_ = true; lastTest_ = observed_;
  return true;
}
void SettingsController::retryClose() {
  if (observed_.phase != WifiPhase::kReleaseFailed || observed_.expired) return;
  observed_ = wifi_.close(requestId_);
  if (observed_.operation == WifiOperation::kTest) lastTest_ = observed_;
}
bool SettingsController::tick(uint32_t now) {
  wifi_.tick(now);
  const auto next = wifi_.status(requestId_);
  const auto availability = wifi_.canConnect();
  const bool changed = availability != availability_ || next.phase != observed_.phase || next.outcome != observed_.outcome ||
      next.expired != observed_.expired || next.ip != observed_.ip;
  availability_ = availability;
  observed_ = next;
  if (!next.expired && next.operation == WifiOperation::kTest) lastTest_ = next;
  if (changed && !next.expired && next.operation == WifiOperation::kScan && next.outcome == WifiOutcome::kSucceeded)
    scans_ = wifi_.scanResults();
  if (changed && !next.expired && next.outcome != WifiOutcome::kNone) {
    wifiMessage_ = next.operation == WifiOperation::kScan ? "扫描" : "测试";
    wifiMessage_ += outcomeText(next.outcome);
    wifiMessage_ += next.phase == WifiPhase::kOff ? "，Wi-Fi已关闭" : "，Wi-Fi关闭失败";
  }
  return changed;
}
void SettingsController::selectNetwork(size_t index) {
  // Selecting a network changes only SSID, preserving account and password drafts.
  if (index < scans_.count) setField(0, scans_.networks[index].ssid);
}
std::string SettingsController::wifiSummary() const {
  if (wifi_.canConnect() == WifiAvailability::kReleaseFailed) return "Wi-Fi关闭失败";
  if (wifi_.canConnect() == WifiAvailability::kBusy) {
    if (!observed_.expired && observed_.phase == WifiPhase::kScanning) return "扫描中";
    if (!observed_.expired && observed_.phase == WifiPhase::kConnecting) return "测试中";
    return "Wi-Fi使用中";
  }
  const auto status = wifi_.configurationStatus();
  if (status == ConfigStatus::kNotFound) return "未配置";
  if (status != ConfigStatus::kOk) return storageMessage(status);
  return savedValid_ ? "已配置（静默）" : "未配置";
}
std::string SettingsController::wifiDetails() const {
  std::string text = wifiMessage_ + "\n" + wifiSummary();
  text += savedValid_ && draft_ == saved_ ? "\n当前草稿与保存值一致" : "\n当前草稿未保存";
  if (!wifiSaveMessage_.empty()) text += "\n上次保存：" + wifiSaveMessage_;
  if (!observed_.expired && observed_.operation == WifiOperation::kScan) {
    text += "\n上次扫描：" + outcomeText(observed_.outcome);
    if (observed_.outcome == WifiOutcome::kSucceeded && scans_.count == 0) text += "，无结果，可手动输入";
    if (scans_.truncated) text += "\n仅显示信号较强的32项";
  }
  if (haveTest_) {
    // Historical IP is evidence of this attempt, never an online indicator.
    text += "\n上次测试：" + outcomeText(lastTest_.outcome) + "\nSSID：" + tested_.ssid;
    if (!lastTest_.ip.empty()) text += "\n测试IP：" + lastTest_.ip;
    if (lastTest_.phase == WifiPhase::kOff) text += "\n测试已结束，Wi-Fi已关闭";
    if (lastTest_.phase == WifiPhase::kReleaseFailed) text += "\nWi-Fi关闭失败，请重试关闭";
    if (!(draft_ == tested_)) text += "\n配置已修改，需重新测试";
    if (!savedValid_ || !(tested_ == saved_)) text += "\n测试配置未保存";
  }
  return text;
}
}  // namespace adv
