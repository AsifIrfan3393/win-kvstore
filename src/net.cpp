#include "net.hpp"

#ifdef _WIN32
#include <mstcpip.h>
#endif

namespace kvstore::net {

NetContext::NetContext() {
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
    initialized_ = true;
  }
#endif
}

NetContext::~NetContext() {
#ifdef _WIN32
  if (initialized_) {
    WSACleanup();
  }
#endif
}

int close_socket(Socket socket_fd) {
#ifdef _WIN32
  return closesocket(socket_fd);
#else
  return close(socket_fd);
#endif
}

int set_reuseaddr(Socket socket_fd) {
  int opt = 1;
#ifdef _WIN32
  return setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
  return setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
}

int send_data(Socket socket_fd, const char* data, size_t size) {
#ifdef _WIN32
  return send(socket_fd, data, static_cast<int>(size), 0);
#else
  return static_cast<int>(send(socket_fd, data, size, 0));
#endif
}

int recv_data(Socket socket_fd, char* buffer, size_t size) {
#ifdef _WIN32
  return recv(socket_fd, buffer, static_cast<int>(size), 0);
#else
  return static_cast<int>(recv(socket_fd, buffer, size, 0));
#endif
}

} // namespace kvstore::net
