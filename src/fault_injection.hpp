#pragma once

#include <chrono>
#include <random>

namespace kvstore {

class FaultInjector {
 public:
  FaultInjector();
  bool should_fail(double probability);
  void maybe_delay(std::chrono::milliseconds delay);

 private:
  std::mt19937 rng_;
};

} // namespace kvstore
