#pragma once

#include "config.hpp"
#include "fault_injection.hpp"
#include "metrics.hpp"
#include "persistence.hpp"
#include "replication.hpp"
#include "storage.hpp"
#include "thread_pool.hpp"
#include "net.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace kvstore {

class MetricsServer {
 public:
  MetricsServer(uint16_t port, Metrics& metrics);
  ~MetricsServer();

  void start();
  void stop();

 private:
  void run();

  uint16_t port_;
  Metrics& metrics_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

class KvServer {
 public:
  KvServer(const Config& config, ShardedStore& store, ThreadPool& pool, Metrics& metrics,
           WalWriter* wal, ReplicationBroadcaster* replication);
  ~KvServer();

  void start();
  void stop();

 private:
  void accept_loop();
  void handle_connection(int client_fd);
  std::string process_command(const std::string& line);
  void apply_record(const std::string& record);

  Config config_;
  ShardedStore& store_;
  ThreadPool& pool_;
  Metrics& metrics_;
  WalWriter* wal_;
  ReplicationBroadcaster* replication_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  net::Socket listen_fd_ = net::kInvalidSocket;
};

} // namespace kvstore
