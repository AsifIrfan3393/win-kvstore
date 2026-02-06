#include "thread_pool.hpp"

namespace kvstore {

ThreadPool::ThreadPool(size_t threads, size_t max_queue_depth)
    : max_queue_depth_(max_queue_depth) {
  workers_.reserve(threads);
  for (size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this]() { worker(); });
  }
}

ThreadPool::~ThreadPool() {
  shutdown();
}

void ThreadPool::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
  }
  not_empty_.notify_all();
  not_full_.notify_all();
  for (auto& worker_thread : workers_) {
    if (worker_thread.joinable()) {
      worker_thread.join();
    }
  }
}

void ThreadPool::worker() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_empty_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
      if (shutdown_ && queue_.empty()) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop();
    }
    not_full_.notify_one();
    task();
  }
}

} // namespace kvstore
