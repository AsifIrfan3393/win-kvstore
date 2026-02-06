#pragma once

#include <string>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace kvstore::net {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

class NetContext {
 public:
  NetContext();
  ~NetContext();

  NetContext(const NetContext&) = delete;
  NetContext& operator=(const NetContext&) = delete;

 private:
#ifdef _WIN32
  bool initialized_ = false;
#endif
};

int close_socket(Socket socket_fd);
int set_reuseaddr(Socket socket_fd);
int send_data(Socket socket_fd, const char* data, size_t size);
int recv_data(Socket socket_fd, char* buffer, size_t size);

} // namespace kvstore::net
