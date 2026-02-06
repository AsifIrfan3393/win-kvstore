#include "server.hpp"

#include "net.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

namespace kvstore {

namespace {

std::vector<std::string> split(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> parts;
  std::string part;
  while (stream >> part) {
    parts.push_back(part);
  }
  return parts;
}

} // namespace

MetricsServer::MetricsServer(uint16_t port, Metrics& metrics) : port_(port), metrics_(metrics) {}

MetricsServer::~MetricsServer() {
  stop();
}

void MetricsServer::start() {
  running_ = true;
  thread_ = std::thread([this]() { run(); });
}

void MetricsServer::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void MetricsServer::run() {
  net::Socket fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == net::kInvalidSocket) {
    return;
  }
  net::set_reuseaddr(fd);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    net::close_socket(fd);
    return;
  }
  if (listen(fd, 16) < 0) {
    net::close_socket(fd);
    return;
  }
  while (running_) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    net::Socket client_fd = accept(fd, reinterpret_cast<sockaddr*>(&client), &len);
    if (client_fd == net::kInvalidSocket) {
      continue;
    }
    auto snap = metrics_.snapshot();
    std::ostringstream body;
    body << "{\n";
    body << "  \"get_count\": " << snap.get_count << ",\n";
    body << "  \"put_count\": " << snap.put_count << ",\n";
    body << "  \"del_count\": " << snap.del_count << ",\n";
    body << "  \"batch_count\": " << snap.batch_count << ",\n";
    body << "  \"eviction_count\": " << snap.eviction_count << ",\n";
    body << "  \"memory_bytes\": " << snap.memory_bytes << ",\n";
    body << "  \"wal_bytes\": " << snap.wal_bytes << ",\n";
    body << "  \"snapshot_duration_ms\": " << snap.snapshot_duration_ms << ",\n";
    body << "  \"replication_lag\": " << snap.replication_lag << ",\n";
    body << "  \"p50_us\": " << snap.p50_us << ",\n";
    body << "  \"p95_us\": " << snap.p95_us << ",\n";
    body << "  \"p99_us\": " << snap.p99_us << "\n";
    body << "}\n";
    std::string body_str = body.str();
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body_str.size() << "\r\n\r\n";
    response << body_str;
    auto resp_str = response.str();
    net::send_data(client_fd, resp_str.data(), resp_str.size());
    net::close_socket(client_fd);
  }
  net::close_socket(fd);
}

KvServer::KvServer(const Config& config, ShardedStore& store, ThreadPool& pool, Metrics& metrics,
                   WalWriter* wal, ReplicationBroadcaster* replication)
    : config_(config), store_(store), pool_(pool), metrics_(metrics), wal_(wal), replication_(replication) {}

KvServer::~KvServer() {
  stop();
}

void KvServer::start() {
  running_ = true;
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == net::kInvalidSocket) {
    throw std::runtime_error("failed to create socket");
  }
  net::set_reuseaddr(listen_fd_);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(config_.port);
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error("failed to bind socket");
  }
  if (listen(listen_fd_, 128) < 0) {
    throw std::runtime_error("failed to listen on socket");
  }
  accept_thread_ = std::thread([this]() { accept_loop(); });
}

void KvServer::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (listen_fd_ != net::kInvalidSocket) {
    net::close_socket(listen_fd_);
    listen_fd_ = net::kInvalidSocket;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

void KvServer::accept_loop() {
  while (running_) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    net::Socket client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &len);
    if (client_fd == net::kInvalidSocket) {
      continue;
    }
    std::thread(&KvServer::handle_connection, this, client_fd).detach();
  }
}

void KvServer::handle_connection(int client_fd) {
  std::string buffer;
  buffer.reserve(2048);
  char temp[1024];
  while (true) {
    int n = net::recv_data(client_fd, temp, sizeof(temp));
    if (n <= 0) {
      break;
    }
    buffer.append(temp, temp + n);
    size_t pos = 0;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      if (line.empty()) {
        continue;
      }
      auto start = std::chrono::steady_clock::now();
      std::vector<std::string> batch_lines;
      std::string response;
      bool precomputed = false;
      if (line.rfind("BATCH", 0) == 0) {
        auto parts = split(line);
        if (parts.size() != 2) {
          response = "ERROR invalid batch";
          precomputed = true;
        } else {
          size_t count = std::stoul(parts[1]);
          while (batch_lines.size() < count) {
            size_t end = buffer.find('\n');
            if (end == std::string::npos) {
              int more = net::recv_data(client_fd, temp, sizeof(temp));
              if (more <= 0) {
                break;
              }
              buffer.append(temp, temp + more);
              continue;
            }
            batch_lines.push_back(buffer.substr(0, end));
            buffer.erase(0, end + 1);
          }
          response = "OK";
          precomputed = true;
        }
      }
      if (!precomputed) {
        auto future = pool_.submit([this, line]() { return process_command(line); });
        response = future.get();
      } else if (!batch_lines.empty()) {
        auto future = pool_.submit([this, batch_lines]() {
          for (const auto& cmd : batch_lines) {
            process_command(cmd);
          }
          metrics_.record_batch();
          return std::string("OK");
        });
        response = future.get();
      }
      auto duration = std::chrono::steady_clock::now() - start;
      metrics_.record_latency(std::chrono::duration_cast<std::chrono::nanoseconds>(duration));
      response.push_back('\n');
      net::send_data(client_fd, response.data(), response.size());
    }
  }
  net::close_socket(client_fd);
}

std::string KvServer::process_command(const std::string& line) {
  auto parts = split(line);
  if (parts.empty()) {
    return "ERROR empty";
  }
  const std::string& cmd = parts[0];
  if (cmd == "GET") {
    if (parts.size() < 2) {
      return "ERROR usage GET key [version]";
    }
    std::optional<uint64_t> version;
    if (parts.size() >= 3) {
      version = std::stoull(parts[2]);
    }
    auto result = store_.get(parts[1], version);
    metrics_.record_get();
    if (!result) {
      return "NOT_FOUND";
    }
    return "VALUE " + *result;
  }
  if (cmd == "PUT") {
    if (config_.role == "replica") {
      return "ERROR read_only";
    }
    if (parts.size() < 3) {
      return "ERROR usage PUT key value [ttl]";
    }
    std::optional<uint32_t> ttl;
    if (parts.size() >= 4) {
      ttl = static_cast<uint32_t>(std::stoul(parts[3]));
    }
    store_.put(parts[1], parts[2], ttl);
    metrics_.record_put();
    if (wal_) {
      wal_->append(line);
    }
    if (replication_) {
      replication_->publish(line);
    }
    return "OK";
  }
  if (cmd == "DEL") {
    if (config_.role == "replica") {
      return "ERROR read_only";
    }
    if (parts.size() < 2) {
      return "ERROR usage DEL key";
    }
    bool removed = store_.del(parts[1]);
    metrics_.record_del();
    if (wal_) {
      wal_->append(line);
    }
    if (replication_) {
      replication_->publish(line);
    }
    return removed ? "OK" : "NOT_FOUND";
  }
  if (cmd == "REBALANCE") {
    if (config_.role == "replica") {
      return "ERROR read_only";
    }
    if (parts.size() != 2) {
      return "ERROR usage REBALANCE shard_count";
    }
    store_.rebalance(static_cast<uint32_t>(std::stoul(parts[1])));
    return "OK";
  }
  if (cmd == "PING") {
    return "PONG";
  }
  return "ERROR unknown command";
}

} // namespace kvstore
