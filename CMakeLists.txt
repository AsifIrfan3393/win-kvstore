cmake_minimum_required(VERSION 3.20)
project(kvstore CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kvstore
  src/main.cpp
  src/server.cpp
  src/thread_pool.cpp
  src/storage.cpp
  src/persistence.cpp
  src/replication.cpp
  src/metrics.cpp
  src/fault_injection.cpp
  src/config.cpp
  src/net.cpp
)

target_include_directories(kvstore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(kvstore PRIVATE -Wall -Wextra -Wpedantic)

add_executable(kvbench
  src/benchmark_main.cpp
  src/benchmark.cpp
  src/thread_pool.cpp
  src/metrics.cpp
  src/fault_injection.cpp
  src/config.cpp
  src/net.cpp
)

if (WIN32)
  target_link_libraries(kvstore PRIVATE ws2_32)
  target_link_libraries(kvbench PRIVATE ws2_32)
endif ()
target_include_directories(kvbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(kvbench PRIVATE -Wall -Wextra -Wpedantic)
