#include "platform/config_file_store.h"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#ifdef ARDUINO
#include <LittleFS.h>
#endif

namespace adv {
const char* configStatusName(ConfigStatus status) {
  switch (status) {
    case ConfigStatus::kOk: return "OK";
    case ConfigStatus::kNotMounted: return "NotMounted";
    case ConfigStatus::kNotFound: return "NotFound";
    case ConfigStatus::kReadFailed: return "ReadFailed";
    case ConfigStatus::kInvalidConfig: return "InvalidConfig";
    case ConfigStatus::kWriteFailed: return "WriteFailed";
    case ConfigStatus::kReloadFailed: return "ReloadFailed";
    case ConfigStatus::kApplyFailed: return "ApplyFailed";
  }
  return "Unknown";
}

bool PlatformConfigFileStore::begin() {
#ifdef ARDUINO
  // A missing/unreadable filesystem must never erase other module configuration.
  mounted_ = LittleFS.begin(false, root_.c_str());
#else
  struct stat info{};
  mounted_ = stat(root_.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
  return mounted_;
}

ConfigStatus PlatformConfigFileStore::read(const std::string& path, std::string& out) {
  if (!mounted_) return ConfigStatus::kNotMounted;
  const auto full = root_ + path;
  struct stat info{};
  if (stat(full.c_str(), &info) != 0) {
    return errno == ENOENT ? ConfigStatus::kNotFound : ConfigStatus::kReadFailed;
  }
  if (!S_ISREG(info.st_mode)) return ConfigStatus::kReadFailed;
  if (info.st_size < 0 || info.st_size > static_cast<long>(kMaxConfigBytes)) {
    return ConfigStatus::kInvalidConfig;
  }
  FILE* file = fopen(full.c_str(), "rb");
  if (!file) return ConfigStatus::kReadFailed;
  char bytes[kMaxConfigBytes + 1];
  const size_t size = fread(bytes, 1, sizeof(bytes), file);
  const bool failed = ferror(file) != 0;
  const bool closed = fclose(file) == 0;
  if (size > kMaxConfigBytes) return ConfigStatus::kInvalidConfig;
  if (failed || !closed || size != static_cast<size_t>(info.st_size)) return ConfigStatus::kReadFailed;
  out.assign(bytes, size);
  return ConfigStatus::kOk;
}

bool PlatformConfigFileStore::writeTemporary(const std::string& path, const std::string& bytes) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file) return false;
  const bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  const bool flushed = fflush(file) == 0;
  const bool closed = fclose(file) == 0;
  return written && flushed && closed;
}

bool PlatformConfigFileStore::renameFile(const std::string& from, const std::string& to) {
  return std::rename(from.c_str(), to.c_str()) == 0;
}

ConfigStatus PlatformConfigFileStore::replace(const std::string& path, const std::string& bytes) {
  if (!mounted_) return ConfigStatus::kNotMounted;
  if (bytes.empty() || bytes.size() > kMaxConfigBytes) return ConfigStatus::kInvalidConfig;
  const auto parent = root_ + path.substr(0, path.find_last_of('/'));
  if (mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) return ConfigStatus::kWriteFailed;
  const auto temporary = path + ".tmp";
  if (!writeTemporary(root_ + temporary, bytes)) return ConfigStatus::kWriteFailed;
  // Verify contents before commit. Never remove the valid destination before rename.
  std::string verified;
  if (read(temporary, verified) != ConfigStatus::kOk || verified != bytes ||
      !renameFile(root_ + temporary, root_ + path)) return ConfigStatus::kWriteFailed;
  return ConfigStatus::kOk;
}
}  // namespace adv
