#pragma once
#include <deque>
#include <vector>
#include "platform/power_adapter.h"
struct FakePower : adv::PowerAdapter {
  struct Step { bool ok; uint32_t actual; };
  uint32_t actual{240};
  bool fail{false};
  std::deque<Step> steps;
  std::vector<uint32_t> calls;
  uint32_t frequencyMhz() const override { return actual; }
  bool applyFrequency(uint32_t mhz) override {
    calls.push_back(mhz);
    if (!steps.empty()) {
      const auto step = steps.front(); steps.pop_front(); actual = step.actual; return step.ok;
    }
    if (fail) return false;
    actual = mhz; return true;
  }
};
