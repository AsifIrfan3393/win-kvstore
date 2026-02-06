#include "metrics.hpp"

#include <algorithm>

namespace kvstore {

LatencySampler::LatencySampler(size_t max_samples) : max_samples_(max_samples) {}

void LatencySampler::record(std::chrono::nanoseconds value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (samples_.size() >= max_samples_) {
    samples_.erase(samples_.begin());
  }
  samples_.push_back(static_cast<double>(value.count()) / 1000.0);
}

Percentiles LatencySampler::percentiles() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Percentiles result;
  if (samples_.empty()) {
    return result;
  }
  std::vector<double> sorted = samples_;
  std::sort(sorted.begin(), sorted.end());
  auto at = [&](double percentile) {
    size_t idx = static_cast<size_t>(percentile * (sorted.size() - 1));
    return sorted[idx];
  };
  result.p50 = at(0.50);
  result.p95 = at(0.95);
  result.p99 = at(0.99);
  return result;
}

void Metrics::record_get() { get_count_++; }
void Metrics::record_put() { put_count_++; }
void Metrics::record_del() { del_count_++; }
void Metrics::record_batch() { batch_count_++; }
void Metrics::record_eviction() { eviction_count_++; }

void Metrics::record_latency(std::chrono::nanoseconds latency) {
  latency_sampler_.record(latency);
}

void Metrics::set_memory_bytes(uint64_t bytes) { memory_bytes_ = bytes; }
void Metrics::set_wal_bytes(uint64_t bytes) { wal_bytes_ = bytes; }
void Metrics::set_snapshot_duration(uint64_t ms) { snapshot_duration_ms_ = ms; }
void Metrics::set_replication_lag(uint64_t lag) { replication_lag_ = lag; }

MetricsSnapshot Metrics::snapshot() const {
  MetricsSnapshot snap;
  snap.get_count = get_count_.load();
  snap.put_count = put_count_.load();
  snap.del_count = del_count_.load();
  snap.batch_count = batch_count_.load();
  snap.eviction_count = eviction_count_.load();
  snap.memory_bytes = memory_bytes_.load();
  snap.wal_bytes = wal_bytes_.load();
  snap.snapshot_duration_ms = snapshot_duration_ms_.load();
  snap.replication_lag = replication_lag_.load();
  auto percentiles = latency_sampler_.percentiles();
  snap.p50_us = percentiles.p50;
  snap.p95_us = percentiles.p95;
  snap.p99_us = percentiles.p99;
  return snap;
}

} // namespace kvstore
