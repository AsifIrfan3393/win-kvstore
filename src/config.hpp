#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kvstore {

struct Config {
  std::string bind_host = "0.0.0.0";
  uint16_t port = 9090;
  uint16_t metrics_port = 9100;
  uint16_t replication_port = 9091;
  std::string role = "leader"; // leader or replica
  std::optional<std::string> replica_of; // host:port
  std::vector<std::string> replica_targets; // host:port list
  std::string data_dir = "data";
  bool enable_wal = true;
  uint32_t snapshot_interval_seconds = 30;
  uint32_t ttl_scan_interval_seconds = 5;
  uint32_t shard_count = 16;
  uint64_t memory_budget_bytes = 512ULL * 1024ULL * 1024ULL;
  uint32_t worker_threads = 8;
  uint32_t task_queue_depth = 4096;

  // Fault injection
  uint32_t wal_delay_ms = 0;
  double wal_fail_probability = 0.0;
  uint32_t snapshot_delay_ms = 0;
  uint32_t replication_delay_ms = 0;

  // Benchmark
  uint32_t bench_clients = 4;
  uint32_t bench_threads = 8;
  uint32_t bench_requests = 10000;
  double bench_read_ratio = 0.7;
  double bench_hotspot_ratio = 0.2;
  std::string bench_output = "bench.json";
};

Config parse_args(int argc, char** argv);

} // namespace kvstore
