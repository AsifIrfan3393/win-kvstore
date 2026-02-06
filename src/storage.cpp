#include "storage.hpp"

#include <functional>

namespace kvstore {

ShardedStore::ShardedStore(uint32_t shards, uint64_t memory_budget_bytes, Metrics& metrics)
    : shards_(shards), memory_budget_bytes_(memory_budget_bytes), metrics_(metrics) {}

ShardedStore::Shard& ShardedStore::shard_for(const std::string& key) {
  size_t idx = std::hash<std::string>{}(key) % shards_.size();
  return shards_[idx];
}

const ShardedStore::Shard& ShardedStore::shard_for(const std::string& key) const {
  size_t idx = std::hash<std::string>{}(key) % shards_.size();
  return shards_[idx];
}

void ShardedStore::touch(Shard& shard, const std::string& key, Entry& entry) {
  shard.lru.erase(entry.lru_it);
  shard.lru.push_front(key);
  entry.lru_it = shard.lru.begin();
}

void ShardedStore::remove_entry(Shard& shard, const std::string& key) {
  auto it = shard.map.find(key);
  if (it == shard.map.end()) {
    return;
  }
  memory_usage_bytes_ -= it->second.size_bytes;
  shard.lru.erase(it->second.lru_it);
  shard.map.erase(it);
}

std::optional<std::string> ShardedStore::get(const std::string& key, std::optional<uint64_t> snapshot_version) {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  auto& shard = shard_for(key);
  std::shared_lock<std::shared_mutex> lock(shard.mutex);
  auto it = shard.map.find(key);
  if (it == shard.map.end()) {
    return std::nullopt;
  }
  const Entry& entry = it->second;
  if (snapshot_version && entry.version > *snapshot_version) {
    return std::nullopt;
  }
  if (entry.expire_at && std::chrono::steady_clock::now() >= *entry.expire_at) {
    return std::nullopt;
  }
  return entry.value;
}

void ShardedStore::put(const std::string& key, std::string value, std::optional<uint32_t> ttl_seconds) {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  auto& shard = shard_for(key);
  std::unique_lock<std::shared_mutex> lock(shard.mutex);
  auto now = std::chrono::steady_clock::now();
  auto expire_at = ttl_seconds ? std::optional<std::chrono::steady_clock::time_point>(now + std::chrono::seconds(*ttl_seconds))
                               : std::nullopt;
  auto it = shard.map.find(key);
  uint64_t version = ++version_;
  size_t size = key.size() + value.size();
  if (it == shard.map.end()) {
    shard.lru.push_front(key);
    Entry entry{std::move(value), version, expire_at, size, shard.lru.begin()};
    shard.map.emplace(key, std::move(entry));
    memory_usage_bytes_ += size;
  } else {
    memory_usage_bytes_ -= it->second.size_bytes;
    it->second.value = std::move(value);
    it->second.version = version;
    it->second.expire_at = expire_at;
    it->second.size_bytes = size;
    touch(shard, key, it->second);
    memory_usage_bytes_ += size;
  }
  enforce_memory_budget();
}

bool ShardedStore::del(const std::string& key) {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  auto& shard = shard_for(key);
  std::unique_lock<std::shared_mutex> lock(shard.mutex);
  auto it = shard.map.find(key);
  if (it == shard.map.end()) {
    return false;
  }
  remove_entry(shard, key);
  return true;
}

uint64_t ShardedStore::current_version() const {
  return version_.load();
}

std::vector<SnapshotItem> ShardedStore::snapshot(uint64_t version) {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  std::vector<SnapshotItem> items;
  for (auto& shard : shards_) {
    std::shared_lock<std::shared_mutex> lock(shard.mutex);
    for (const auto& [key, entry] : shard.map) {
      if (entry.version <= version) {
        items.push_back({key, entry.value, entry.version, entry.expire_at});
      }
    }
  }
  return items;
}

void ShardedStore::restore(const std::vector<SnapshotItem>& items) {
  std::unique_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  for (const auto& item : items) {
    auto& shard = shard_for(item.key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    size_t size = item.key.size() + item.value.size();
    shard.lru.push_front(item.key);
    Entry entry{item.value, item.version, item.expire_at, size, shard.lru.begin()};
    shard.map[item.key] = std::move(entry);
    memory_usage_bytes_ += size;
    if (item.version > version_) {
      version_ = item.version;
    }
  }
  enforce_memory_budget();
}

void ShardedStore::expire_keys() {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  auto now = std::chrono::steady_clock::now();
  for (auto& shard : shards_) {
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    for (auto it = shard.map.begin(); it != shard.map.end();) {
      if (it->second.expire_at && now >= *(it->second.expire_at)) {
        memory_usage_bytes_ -= it->second.size_bytes;
        shard.lru.erase(it->second.lru_it);
        it = shard.map.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void ShardedStore::enforce_memory_budget() {
  std::shared_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  while (memory_usage_bytes_.load() > memory_budget_bytes_) {
    bool evicted = false;
    for (auto& shard : shards_) {
      std::unique_lock<std::shared_mutex> lock(shard.mutex);
      if (!shard.lru.empty()) {
        std::string key = shard.lru.back();
        remove_entry(shard, key);
        metrics_.record_eviction();
        evicted = true;
        break;
      }
    }
    if (!evicted) {
      break;
    }
  }
  metrics_.set_memory_bytes(memory_usage_bytes_.load());
}

uint64_t ShardedStore::memory_usage() const {
  return memory_usage_bytes_.load();
}

void ShardedStore::rebalance(uint32_t new_shard_count) {
  if (new_shard_count == 0 || new_shard_count == shards_.size()) {
    return;
  }
  std::unique_lock<std::shared_mutex> rebalance_lock(rebalance_mutex_);
  std::vector<Shard> new_shards(new_shard_count);
  for (auto& shard : shards_) {
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    for (auto& [key, entry] : shard.map) {
      size_t idx = std::hash<std::string>{}(key) % new_shards.size();
      auto& target = new_shards[idx];
      std::unique_lock<std::shared_mutex> target_lock(target.mutex);
      target.lru.push_front(key);
      entry.lru_it = target.lru.begin();
      target.map.emplace(key, std::move(entry));
    }
    shard.map.clear();
    shard.lru.clear();
  }
  shards_ = std::move(new_shards);
}

} // namespace kvstore
