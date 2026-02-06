#pragma once

#include "metrics.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kvstore {

struct SnapshotItem {
  std::string key;
  std::string value;
  uint64_t version;
  std::optional<std::chrono::steady_clock::time_point> expire_at;
};

class ShardedStore {
 public:
  ShardedStore(uint32_t shards, uint64_t memory_budget_bytes, Metrics& metrics);

  std::optional<std::string> get(const std::string& key, std::optional<uint64_t> snapshot_version = std::nullopt);
  void put(const std::string& key, std::string value, std::optional<uint32_t> ttl_seconds);
  bool del(const std::string& key);

  uint64_t current_version() const;
  std::vector<SnapshotItem> snapshot(uint64_t version);
  void restore(const std::vector<SnapshotItem>& items);

  void expire_keys();
  void enforce_memory_budget();
  uint64_t memory_usage() const;
  void rebalance(uint32_t new_shard_count);

 private:
  struct Entry {
    std::string value;
    uint64_t version;
    std::optional<std::chrono::steady_clock::time_point> expire_at;
    size_t size_bytes;
    std::list<std::string>::iterator lru_it;
  };

  struct Shard {
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string, Entry> map;
    std::list<std::string> lru;
  };

  Shard& shard_for(const std::string& key);
  const Shard& shard_for(const std::string& key) const;
  void touch(Shard& shard, const std::string& key, Entry& entry);
  void remove_entry(Shard& shard, const std::string& key);

  std::vector<Shard> shards_;
  mutable std::shared_mutex rebalance_mutex_;
  uint64_t memory_budget_bytes_;
  std::atomic<uint64_t> memory_usage_bytes_{0};
  std::atomic<uint64_t> version_{0};
  Metrics& metrics_;
};

} // namespace kvstore
