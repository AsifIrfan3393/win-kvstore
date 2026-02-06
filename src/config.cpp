#include "config.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace kvstore {

namespace {

bool consume_flag(int& i, int argc, char** argv, std::string_view flag, std::string& value) {
  if (std::string_view(argv[i]) == flag && i + 1 < argc) {
    value = argv[++i];
    return true;
  }
  return false;
}

bool consume_flag(int& i, int argc, char** argv, std::string_view flag, uint32_t& value) {
  if (std::string_view(argv[i]) == flag && i + 1 < argc) {
    value = static_cast<uint32_t>(std::stoul(argv[++i]));
    return true;
  }
  return false;
}

bool consume_flag(int& i, int argc, char** argv, std::string_view flag, uint16_t& value) {
  if (std::string_view(argv[i]) == flag && i + 1 < argc) {
    value = static_cast<uint16_t>(std::stoul(argv[++i]));
    return true;
  }
  return false;
}

bool consume_flag(int& i, int argc, char** argv, std::string_view flag, uint64_t& value) {
  if (std::string_view(argv[i]) == flag && i + 1 < argc) {
    value = static_cast<uint64_t>(std::stoull(argv[++i]));
    return true;
  }
  return false;
}

bool consume_flag(int& i, int argc, char** argv, std::string_view flag, double& value) {
  if (std::string_view(argv[i]) == flag && i + 1 < argc) {
    value = std::stod(argv[++i]);
    return true;
  }
  return false;
}

} // namespace

Config parse_args(int argc, char** argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (consume_flag(i, argc, argv, "--bind", config.bind_host)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--port", config.port)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--metrics-port", config.metrics_port)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--replication-port", config.replication_port)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--role", config.role)) {
      continue;
    }
    {
      std::string value;
      if (consume_flag(i, argc, argv, "--replica-of", value)) {
        config.replica_of = value;
        continue;
      }
      if (consume_flag(i, argc, argv, "--replica-target", value)) {
        config.replica_targets.push_back(value);
        continue;
      }
    }
    if (consume_flag(i, argc, argv, "--data-dir", config.data_dir)) {
      continue;
    }
    if (arg == "--disable-wal") {
      config.enable_wal = false;
      continue;
    }
    if (consume_flag(i, argc, argv, "--snapshot-interval", config.snapshot_interval_seconds)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--ttl-scan", config.ttl_scan_interval_seconds)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--shards", config.shard_count)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--memory-budget", config.memory_budget_bytes)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--workers", config.worker_threads)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--queue-depth", config.task_queue_depth)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--wal-delay", config.wal_delay_ms)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--wal-fail-prob", config.wal_fail_probability)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--snapshot-delay", config.snapshot_delay_ms)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--replication-delay", config.replication_delay_ms)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-clients", config.bench_clients)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-threads", config.bench_threads)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-requests", config.bench_requests)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-read-ratio", config.bench_read_ratio)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-hotspot", config.bench_hotspot_ratio)) {
      continue;
    }
    if (consume_flag(i, argc, argv, "--bench-output", config.bench_output)) {
      continue;
    }
    std::cerr << "Unknown argument: " << arg << "\n";
  }
  return config;
}

} // namespace kvstore
