#pragma once

#include <cstddef>
#include <string>

namespace adv {
enum class ConfigStatus {
  kOk, kNotMounted, kNotFound, kReadFailed, kInvalidConfig,
  kWriteFailed, kReloadFailed, kApplyFailed
};
constexpr size_t kMaxConfigBytes = 512;
const char* configStatusName(ConfigStatus status);

class ConfigFileStore {
 public:
  virtual ~ConfigFileStore() = default;
  virtual ConfigStatus read(const std::string& path, std::string& out) = 0;
  virtual ConfigStatus replace(const std::string& path, const std::string& bytes) = 0;
};

// POSIX file operations also exercise the device VFS path in native tests.
class PlatformConfigFileStore : public ConfigFileStore {
 public:
  explicit PlatformConfigFileStore(std::string root = "/littlefs") : root_(std::move(root)) {}
  bool begin();
  ConfigStatus read(const std::string& path, std::string& out) override;
  ConfigStatus replace(const std::string& path, const std::string& bytes) override;
 protected:
  virtual bool writeTemporary(const std::string& path, const std::string& bytes);
  virtual bool renameFile(const std::string& from, const std::string& to);
 private:
  std::string root_;
  bool mounted_{false};
};
}  // namespace adv
