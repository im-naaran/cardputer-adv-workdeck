#pragma once
#include <cstdint>

namespace adv {
constexpr bool validCpuFrequency(uint32_t mhz) { return mhz == 80 || mhz == 160 || mhz == 240; }
enum class PowerStatus { kOk, kInvalidFrequency, kUnsupported, kApplyFailed, kRestoreFailed };
struct PowerResult {
  PowerStatus status{PowerStatus::kOk};
  uint32_t frequencyMhz{0};
};
class PowerAdapter {
 public:
  virtual ~PowerAdapter() = default;
  PowerResult configure(uint32_t frequency, bool allowAutomaticSleep = false);
  virtual uint32_t frequencyMhz() const = 0;
 protected:
  virtual bool applyFrequency(uint32_t frequency) = 0;
};
class PlatformPowerAdapter : public PowerAdapter {
 public:
  uint32_t frequencyMhz() const override;
 protected:
  bool applyFrequency(uint32_t frequency) override;
};
}  // namespace adv
