#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace kvstore {

struct Percentiles {
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
};

class LatencySampler {
 public:
  explicit LatencySampler(size_t max_samples = 10000);
  void record(std::chrono::nanoseconds value);
  Percentiles percentiles() const;

 private:
  size_t max_samples_;
  mutable std::mutex mutex_;
  std::vector<double> samples_;
};

struct MetricsSnapshot {
  uint64_t get_count = 0;
  uint64_t put_count = 0;
  uint64_t del_count = 0;
  uint64_t batch_count = 0;
  uint64_t eviction_count = 0;
  uint64_t memory_bytes = 0;
  uint64_t wal_bytes = 0;
  uint64_t snapshot_duration_ms = 0;
  uint64_t replication_lag = 0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
};

class Metrics {
 public:
  void record_get();
  void record_put();
  void record_del();
  void record_batch();
  void record_eviction();
  void record_latency(std::chrono::nanoseconds latency);

  void set_memory_bytes(uint64_t bytes);
  void set_wal_bytes(uint64_t bytes);
  void set_snapshot_duration(uint64_t ms);
  void set_replication_lag(uint64_t lag);

  MetricsSnapshot snapshot() const;

 private:
  std::atomic<uint64_t> get_count_{0};
  std::atomic<uint64_t> put_count_{0};
  std::atomic<uint64_t> del_count_{0};
  std::atomic<uint64_t> batch_count_{0};
  std::atomic<uint64_t> eviction_count_{0};
  std::atomic<uint64_t> memory_bytes_{0};
  std::atomic<uint64_t> wal_bytes_{0};
  std::atomic<uint64_t> snapshot_duration_ms_{0};
  std::atomic<uint64_t> replication_lag_{0};
  LatencySampler latency_sampler_;
};

} // namespace kvstore
