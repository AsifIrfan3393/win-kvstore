#pragma once

#include "metrics.hpp"
#include "net.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kvstore {

class ReplicationBroadcaster {
 public:
  ReplicationBroadcaster(uint16_t port, Metrics& metrics, uint32_t delay_ms);
  ~ReplicationBroadcaster();

  void start();
  void stop();
  void publish(const std::string& record);

 private:
  void accept_loop();

  uint16_t port_;
  Metrics& metrics_;
  uint32_t delay_ms_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  net::Socket listen_fd_ = net::kInvalidSocket;
  std::mutex clients_mutex_;
  std::vector<net::Socket> clients_;
  std::atomic<uint64_t> sequence_{0};
  std::atomic<uint64_t> sent_{0};
};

class ReplicationClient {
 public:
  using ApplyFn = std::function<void(const std::string&)>;

  ReplicationClient(std::string host, uint16_t port, ApplyFn apply_fn);
  ~ReplicationClient();

  void start();
  void stop();

 private:
  void run();

  std::string host_;
  uint16_t port_;
  ApplyFn apply_fn_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

} // namespace kvstore
