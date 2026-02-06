#pragma once

#include "config.hpp"
#include "metrics.hpp"
#include "net.hpp"

#include <mutex>
#include <vector>
#include <string>

namespace kvstore {

class BenchmarkRunner {
 public:
  BenchmarkRunner(const Config& config, Metrics& metrics);
  void run();

 private:
  struct ClientConnection {
    net::Socket socket = net::kInvalidSocket;
    std::mutex mutex;
  };

  std::vector<ClientConnection> create_clients(uint32_t count) const;
  void close_clients(std::vector<ClientConnection>& clients) const;

  Config config_;
  Metrics& metrics_;
};

} // namespace kvstore
