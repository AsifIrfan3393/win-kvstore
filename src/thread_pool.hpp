#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace kvstore {

class ThreadPool {
 public:
  ThreadPool(size_t threads, size_t max_queue_depth);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename Fn>
  auto submit(Fn&& fn) -> std::future<decltype(fn())> {
    using Result = decltype(fn());
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
    std::future<Result> future = task->get_future();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_full_.wait(lock, [this] { return shutdown_ || queue_.size() < max_queue_depth_; });
      if (shutdown_) {
        throw std::runtime_error("thread pool is shutting down");
      }
      queue_.emplace([task]() { (*task)(); });
    }
    not_empty_.notify_one();
    return future;
  }

  void shutdown();

 private:
  void worker();

  size_t max_queue_depth_;
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool shutdown_ = false;
};

} // namespace kvstore
