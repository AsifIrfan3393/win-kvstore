#include "persistence.hpp"

#include <chrono>
#include <cstring>
#include <iostream>

namespace kvstore {

namespace {

uint32_t crc32(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : data) {
    crc ^= c;
    for (int i = 0; i < 8; ++i) {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

} // namespace

WalWriter::WalWriter(const std::filesystem::path& path, FaultInjector& fault_injector, Metrics& metrics,
                     uint32_t delay_ms, double fail_probability)
    : path_(path), fault_injector_(fault_injector), metrics_(metrics), delay_ms_(delay_ms),
      fail_probability_(fail_probability) {
  std::filesystem::create_directories(path_.parent_path());
  stream_.open(path_, std::ios::binary | std::ios::app);
}

void WalWriter::append(const std::string& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  fault_injector_.maybe_delay(std::chrono::milliseconds(delay_ms_));
  if (fault_injector_.should_fail(fail_probability_)) {
    throw std::runtime_error("fault injected WAL failure");
  }
  uint32_t len = static_cast<uint32_t>(record.size());
  uint32_t checksum = crc32(record);
  stream_.write(reinterpret_cast<const char*>(&len), sizeof(len));
  stream_.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
  stream_.write(record.data(), record.size());
  stream_.flush();
  metrics_.set_wal_bytes(size_bytes());
}

void WalWriter::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_.flush();
}

uint64_t WalWriter::size_bytes() const {
  std::error_code ec;
  auto size = std::filesystem::file_size(path_, ec);
  return ec ? 0 : size;
}

WalReader::WalReader(const std::filesystem::path& path) : path_(path) {}

std::vector<std::string> WalReader::read_all() {
  std::vector<std::string> records;
  std::ifstream stream(path_, std::ios::binary);
  if (!stream.is_open()) {
    return records;
  }
  while (true) {
    uint32_t len = 0;
    uint32_t checksum = 0;
    stream.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (stream.eof()) {
      break;
    }
    if (!stream) {
      break;
    }
    stream.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
    if (!stream) {
      break;
    }
    std::string data(len, '\0');
    stream.read(data.data(), len);
    if (!stream) {
      break;
    }
    if (crc32(data) != checksum) {
      std::cerr << "WAL checksum mismatch, stopping replay" << std::endl;
      break;
    }
    records.push_back(std::move(data));
  }
  return records;
}

SnapshotManager::SnapshotManager(const std::filesystem::path& dir, FaultInjector& fault_injector, Metrics& metrics,
                                 uint32_t delay_ms)
    : dir_(dir), fault_injector_(fault_injector), metrics_(metrics), delay_ms_(delay_ms) {
  std::filesystem::create_directories(dir_);
}

void SnapshotManager::write_snapshot(const std::vector<SnapshotItem>& items) {
  auto start = std::chrono::steady_clock::now();
  fault_injector_.maybe_delay(std::chrono::milliseconds(delay_ms_));
  auto temp = dir_ / "snapshot.tmp";
  auto final = dir_ / "snapshot.dat";
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  auto now_steady = std::chrono::steady_clock::now();
  for (const auto& item : items) {
    uint32_t key_len = static_cast<uint32_t>(item.key.size());
    uint32_t val_len = static_cast<uint32_t>(item.value.size());
    uint64_t version = item.version;
    int64_t ttl_ms = -1;
    if (item.expire_at) {
      ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(*item.expire_at - now_steady).count();
      if (ttl_ms < 0) {
        ttl_ms = -1;
      }
    }
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&ttl_ms), sizeof(ttl_ms));
    out.write(item.key.data(), item.key.size());
    out.write(item.value.data(), item.value.size());
  }
  out.flush();
  out.close();
  std::filesystem::rename(temp, final);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  metrics_.set_snapshot_duration(static_cast<uint64_t>(duration.count()));
}

std::vector<SnapshotItem> SnapshotManager::load_latest() {
  std::vector<SnapshotItem> items;
  auto file = dir_ / "snapshot.dat";
  std::ifstream in(file, std::ios::binary);
  if (!in.is_open()) {
    return items;
  }
  auto now_steady = std::chrono::steady_clock::now();
  while (true) {
    uint32_t key_len = 0;
    uint32_t val_len = 0;
    uint64_t version = 0;
    int64_t ttl_ms = 0;
    in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    if (in.eof()) {
      break;
    }
    in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&ttl_ms), sizeof(ttl_ms));
    if (!in) {
      break;
    }
    std::string key(key_len, '\0');
    std::string value(val_len, '\0');
    in.read(key.data(), key_len);
    in.read(value.data(), val_len);
    if (!in) {
      break;
    }
    std::optional<std::chrono::steady_clock::time_point> expire_at;
    if (ttl_ms >= 0) {
      expire_at = now_steady + std::chrono::milliseconds(ttl_ms);
    }
    items.push_back({std::move(key), std::move(value), version, expire_at});
  }
  return items;
}

} // namespace kvstore
