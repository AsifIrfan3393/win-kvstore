#pragma once

#include "fault_injection.hpp"
#include "metrics.hpp"
#include "storage.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kvstore {

class WalWriter {
 public:
  WalWriter(const std::filesystem::path& path, FaultInjector& fault_injector, Metrics& metrics,
            uint32_t delay_ms, double fail_probability);
  void append(const std::string& record);
  void flush();
  uint64_t size_bytes() const;

 private:
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::ofstream stream_;
  FaultInjector& fault_injector_;
  Metrics& metrics_;
  uint32_t delay_ms_;
  double fail_probability_;
};

class WalReader {
 public:
  explicit WalReader(const std::filesystem::path& path);
  std::vector<std::string> read_all();

 private:
  std::filesystem::path path_;
};

class SnapshotManager {
 public:
  SnapshotManager(const std::filesystem::path& dir, FaultInjector& fault_injector, Metrics& metrics,
                  uint32_t delay_ms);
  void write_snapshot(const std::vector<SnapshotItem>& items);
  std::vector<SnapshotItem> load_latest();

 private:
  std::filesystem::path dir_;
  FaultInjector& fault_injector_;
  Metrics& metrics_;
  uint32_t delay_ms_;
};

} // namespace kvstore
