#include "benchmark.hpp"
#include "config.hpp"
#include "metrics.hpp"
#include "net.hpp"
#include <fstream>


namespace kvstore {

BenchmarkRunner::BenchmarkRunner(const Config& config, Metrics& metrics)
    : config_(config), metrics_(metrics) {}

void BenchmarkRunner::run() {
  using namespace std::chrono;

  net::NetContext ctx;

  auto start = steady_clock::now();

  for (uint32_t i = 0; i < config_.bench_requests; ++i) {

    net::Socket sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == net::kInvalidSocket) continue;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);

#ifdef _WIN32
    InetPtonA(AF_INET, config_.bind_host.c_str(), &addr.sin_addr);
#else
    inet_pton(AF_INET, config_.bind_host.c_str(), &addr.sin_addr);
#endif

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      net::close_socket(sock);
      continue;
    }

    const char* msg = "PING\n";
    net::send_data(sock, msg, 5);

    char buf[128];
    net::recv_data(sock, buf, sizeof(buf));

    net::close_socket(sock);
  }

  auto end = steady_clock::now();

  auto total_us =
      duration_cast<microseconds>(end - start).count();

  std::ofstream out(config_.bench_output);
  out << "{\n";
  out << "  \"requests\": " << config_.bench_requests << ",\n";
  out << "  \"total_us\": " << total_us << "\n";
  out << "}\n";
}



}
