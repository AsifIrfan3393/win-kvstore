#include "replication.hpp"

#include "net.hpp"

#include <chrono>
#include <cstring>
#include <iostream>

namespace kvstore {

ReplicationBroadcaster::ReplicationBroadcaster(uint16_t port, Metrics& metrics, uint32_t delay_ms)
    : port_(port), metrics_(metrics), delay_ms_(delay_ms) {}

ReplicationBroadcaster::~ReplicationBroadcaster() {
  stop();
}

void ReplicationBroadcaster::start() {
  running_ = true;
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == net::kInvalidSocket) {
    throw std::runtime_error("failed to create replication socket");
  }
  net::set_reuseaddr(listen_fd_);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error("failed to bind replication socket");
  }
  if (listen(listen_fd_, 64) < 0) {
    throw std::runtime_error("failed to listen on replication socket");
  }
  accept_thread_ = std::thread([this]() { accept_loop(); });
}

void ReplicationBroadcaster::stop() {
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
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (auto fd : clients_) {
    net::close_socket(fd);
  }
  clients_.clear();
}

void ReplicationBroadcaster::publish(const std::string& record) {
  if (!running_) {
    return;
  }
  uint64_t seq = ++sequence_;
  std::string payload = record + "\n";
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
      if (delay_ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
      }
      int sent = net::send_data(*it, payload.data(), payload.size());
      if (sent <= 0) {
        net::close_socket(*it);
        it = clients_.erase(it);
      } else {
        ++it;
      }
    }
  }
  sent_ = seq;
  metrics_.set_replication_lag(sequence_.load() - sent_.load());
}

void ReplicationBroadcaster::accept_loop() {
  while (running_) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    net::Socket client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
    if (client_fd == net::kInvalidSocket) {
      if (running_) {
        continue;
      }
      break;
    }
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.push_back(client_fd);
  }
}

ReplicationClient::ReplicationClient(std::string host, uint16_t port, ApplyFn apply_fn)
    : host_(std::move(host)), port_(port), apply_fn_(std::move(apply_fn)) {}

ReplicationClient::~ReplicationClient() {
  stop();
}

void ReplicationClient::start() {
  running_ = true;
  thread_ = std::thread([this]() { run(); });
}

void ReplicationClient::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ReplicationClient::run() {
  while (running_) {
    net::Socket fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == net::kInvalidSocket) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
      net::close_socket(fd);
      return;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      net::close_socket(fd);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    std::string buffer;
    buffer.reserve(1024);
    char temp[512];
    while (running_) {
      int n = net::recv_data(fd, temp, sizeof(temp));
      if (n <= 0) {
        break;
      }
      buffer.append(temp, temp + n);
      size_t pos = 0;
      while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        if (!line.empty()) {
          apply_fn_(line);
        }
      }
    }
    net::close_socket(fd);
  }
}

} // namespace kvstore
