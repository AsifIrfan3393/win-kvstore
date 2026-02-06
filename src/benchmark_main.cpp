#include "benchmark.hpp"
#include "config.hpp"
#include "metrics.hpp"

int main(int argc, char** argv) {
  kvstore::Config config = kvstore::parse_args(argc, argv);
  kvstore::Metrics metrics;
  kvstore::BenchmarkRunner runner(config, metrics);
  runner.run();
  return 0;
}
