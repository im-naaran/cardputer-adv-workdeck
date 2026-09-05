#pragma once
#include <cstdint>
#include <ctime>
#include <limits>
namespace adv {
class SystemClock {
 public:
  virtual ~SystemClock() = default;
  virtual bool setUtcMilliseconds(int64_t value) = 0;
  virtual bool readUtcMilliseconds(int64_t& value) const = 0;
  virtual int64_t maxSeconds() const { return std::numeric_limits<time_t>::max(); }
  virtual int64_t minSeconds() const { return std::numeric_limits<time_t>::min(); }
};
class PlatformSystemClock : public SystemClock {
 public:
  bool setUtcMilliseconds(int64_t value) override;
  bool readUtcMilliseconds(int64_t& value) const override;
};
}
