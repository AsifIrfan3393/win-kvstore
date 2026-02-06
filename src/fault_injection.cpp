#include "fault_injection.hpp"

#include <thread>

namespace kvstore {

FaultInjector::FaultInjector() : rng_(std::random_device{}()) {}

bool FaultInjector::should_fail(double probability) {
  if (probability <= 0.0) {
    return false;
  }
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(rng_) < probability;
}

void FaultInjector::maybe_delay(std::chrono::milliseconds delay) {
  if (delay.count() > 0) {
    std::this_thread::sleep_for(delay);
  }
}

} // namespace kvstore
