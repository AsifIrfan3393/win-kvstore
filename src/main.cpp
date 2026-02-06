#include "config.hpp"
#include "fault_injection.hpp"
#include "metrics.hpp"
#include "persistence.hpp"
#include "replication.hpp"
#include "server.hpp"
#include "storage.hpp"
#include "thread_pool.hpp"
#include "net.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace kvstore {

void apply_record(ShardedStore& store, const std::string& record) {
  std::istringstream stream(record);
  std::string cmd;
  stream >> cmd;
  if (cmd == "PUT") {
    std::string key;
    std::string value;
    stream >> key >> value;
    std::optional<uint32_t> ttl;
    if (!stream.eof()) {
      uint32_t ttl_value;
      if (stream >> ttl_value) {
        ttl = ttl_value;
      }
    }
    if (!key.empty()) {
      store.put(key, value, ttl);
    }
  } else if (cmd == "DEL") {
    std::string key;
    stream >> key;
    if (!key.empty()) {
      store.del(key);
    }
  }
}

} // namespace kvstore

namespace {
std::atomic<bool>* g_running = nullptr;

void handle_signal(int) {
  if (g_running) {
    g_running->store(false);
  }
}
} // namespace

int main(int argc, char** argv) {
  kvstore::Config config = kvstore::parse_args(argc, argv);
  kvstore::net::NetContext net_context;
  kvstore::Metrics metrics;
  kvstore::FaultInjector fault_injector;
  kvstore::ThreadPool pool(config.worker_threads, config.task_queue_depth);
  kvstore::ShardedStore store(config.shard_count, config.memory_budget_bytes, metrics);

  std::filesystem::create_directories(config.data_dir);
  kvstore::SnapshotManager snapshot_manager(config.data_dir, fault_injector, metrics, config.snapshot_delay_ms);
  kvstore::WalWriter* wal_writer = nullptr;
  std::unique_ptr<kvstore::WalWriter> wal_holder;
  if (config.enable_wal) {
    wal_holder = std::make_unique<kvstore::WalWriter>(
        std::filesystem::path(config.data_dir) / "wal.log", fault_injector, metrics, config.wal_delay_ms,
        config.wal_fail_probability);
    wal_writer = wal_holder.get();
  }

  auto snapshot_items = snapshot_manager.load_latest();
  if (!snapshot_items.empty()) {
    store.restore(snapshot_items);
  }
  if (config.enable_wal) {
    kvstore::WalReader wal_reader(std::filesystem::path(config.data_dir) / "wal.log");
    auto records = wal_reader.read_all();
    for (const auto& record : records) {
      kvstore::apply_record(store, record);
    }
  }

  kvstore::ReplicationBroadcaster* broadcaster = nullptr;
  std::unique_ptr<kvstore::ReplicationBroadcaster> broadcaster_holder;
  if (config.role == "leader") {
    broadcaster_holder = std::make_unique<kvstore::ReplicationBroadcaster>(config.replication_port, metrics,
                                                                           config.replication_delay_ms);
    broadcaster_holder->start();
    broadcaster = broadcaster_holder.get();
  }

  std::unique_ptr<kvstore::ReplicationClient> replica_client;
  if (config.role == "replica" && config.replica_of) {
    auto pos = config.replica_of->find(':');
    std::string host = config.replica_of->substr(0, pos);
    uint16_t port = static_cast<uint16_t>(std::stoi(config.replica_of->substr(pos + 1)));
    replica_client = std::make_unique<kvstore::ReplicationClient>(host, port, [&store](const std::string& record) {
      kvstore::apply_record(store, record);
    });
    replica_client->start();
  }

  kvstore::MetricsServer metrics_server(config.metrics_port, metrics);
  metrics_server.start();

  kvstore::KvServer server(config, store, pool, metrics, wal_writer, broadcaster);
  server.start();

  std::atomic<bool> running{true};
  g_running = &running;
  std::thread ttl_thread([&]() {
    while (running) {
      store.expire_keys();
      std::this_thread::sleep_for(std::chrono::seconds(config.ttl_scan_interval_seconds));
    }
  });

  std::thread snapshot_thread([&]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(config.snapshot_interval_seconds));
      auto version = store.current_version();
      auto items = store.snapshot(version);
      snapshot_manager.write_snapshot(items);
    }
  });

  std::cout << "KV store running on port " << config.port << " with metrics on " << config.metrics_port << "\n";
  std::cout << "Role: " << config.role << "\n";

  std::signal(SIGINT, handle_signal);
  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  ttl_thread.join();
  snapshot_thread.join();
  metrics_server.stop();
  server.stop();
  if (replica_client) {
    replica_client->stop();
  }
  if (broadcaster) {
    broadcaster->stop();
  }
  return 0;
}
