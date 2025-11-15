// Repository: Retrovue-air
// Component: TS Output Sink Implementation
// Purpose: Unix Domain Socket sink for MPEG-TS stream output.
// Copyright (c) 2025 RetroVue

#include "retrovue/playout_sinks/mpegts/TsOutputSink.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>

namespace retrovue::playout_sinks::mpegts {

TsOutputSink::TsOutputSink(const std::string& socket_path)
    : socket_path_(socket_path),
      listen_fd_(-1),
      client_fd_(-1),
      client_connected_(false),
      running_(false),
      stop_requested_(false) {
}

TsOutputSink::~TsOutputSink() {
  Stop();
}

bool TsOutputSink::Initialize() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  // Unlink existing socket file if it exists
  if (std::filesystem::exists(socket_path_)) {
    if (unlink(socket_path_.c_str()) < 0) {
      std::cerr << "[TsOutputSink] Failed to unlink existing socket: " 
                << strerror(errno) << std::endl;
      return false;
    }
    std::cout << "[TsOutputSink] Unlinked existing socket file: " << socket_path_ << std::endl;
  }

  // Create parent directory if it doesn't exist
  std::filesystem::path socket_dir = std::filesystem::path(socket_path_).parent_path();
  if (!socket_dir.empty() && !std::filesystem::exists(socket_dir)) {
    try {
      std::filesystem::create_directories(socket_dir);
      std::cout << "[TsOutputSink] Created socket directory: " << socket_dir << std::endl;
    } catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "[TsOutputSink] Failed to create socket directory: " << e.what() << std::endl;
      return false;
    }
  }

  // Create Unix domain socket
  listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    std::cerr << "[TsOutputSink] Failed to create socket: " 
              << strerror(errno) << std::endl;
    return false;
  }

  // Set socket to non-blocking for accept operations
  int flags = fcntl(listen_fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    std::cerr << "[TsOutputSink] Failed to set non-blocking: " 
              << strerror(errno) << std::endl;
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Bind to socket path
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (socket_path_.length() >= sizeof(addr.sun_path)) {
    std::cerr << "[TsOutputSink] Socket path too long: " << socket_path_ << std::endl;
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[TsOutputSink] Failed to bind to " << socket_path_ << ": " 
              << strerror(errno) << std::endl;
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Listen for connections (backlog of 1 - only accept one client at a time)
  if (listen(listen_fd_, 1) < 0) {
    std::cerr << "[TsOutputSink] Failed to listen: " 
              << strerror(errno) << std::endl;
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  std::cout << "[TsOutputSink] Listening on Unix domain socket: " << socket_path_ << std::endl;
  return true;
}

bool TsOutputSink::Start() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (running_.load(std::memory_order_acquire)) {
    return false;  // Already running
  }

  if (listen_fd_ < 0) {
    std::cerr << "[TsOutputSink] Socket not initialized" << std::endl;
    return false;
  }

  stop_requested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  accept_thread_ = std::thread(&TsOutputSink::AcceptThread, this);

  return true;
}

void TsOutputSink::Stop() {
  if (!running_.load(std::memory_order_acquire)) {
    return;  // Not running
  }

  stop_requested_.store(true, std::memory_order_release);

  // Wait for accept thread to exit
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  CleanupSocket();

  running_.store(false, std::memory_order_release);
}

bool TsOutputSink::Write(const uint8_t* data, size_t size) {
  if (!client_connected_.load(std::memory_order_acquire) || client_fd_ < 0) {
    return false;  // No client connected
  }

  // Use blocking send with MSG_NOSIGNAL to avoid SIGPIPE
  // The socket is set to blocking mode for the client connection to ensure atomic packet writes
  size_t sent = 0;
  while (sent < size) {
    ssize_t result = send(client_fd_, data + sent, size - sent, MSG_NOSIGNAL);
    
    if (result < 0) {
      if (errno == EINTR) {
        // Interrupted by signal - retry
        continue;
      } else if (errno == EPIPE || errno == ECONNRESET) {
        // Client disconnected
        std::cout << "[TsOutputSink] Client disconnected during write (EPIPE/ECONNRESET)" << std::endl;
        HandleClientDisconnect();
        return false;
      } else {
        std::cerr << "[TsOutputSink] Send error: " << strerror(errno) << std::endl;
        HandleClientDisconnect();
        return false;
      }
    } else if (result == 0) {
      // Connection closed
      HandleClientDisconnect();
      return false;
    } else {
      sent += static_cast<size_t>(result);
    }
  }

  return true;  // All data sent
}

bool TsOutputSink::IsClientConnected() const {
  return client_connected_.load(std::memory_order_acquire);
}

void TsOutputSink::AcceptThread() {
  while (running_.load(std::memory_order_acquire) &&
         !stop_requested_.load(std::memory_order_acquire)) {
    TryAcceptClient();
    
    // Sleep briefly to avoid busy-waiting
    if (stop_requested_.load(std::memory_order_acquire)) {
      break;
    }
    
    constexpr int64_t sleep_ms = 100;
    constexpr int64_t chunk_ms = 10;  // Check every 10ms
    int64_t remaining = sleep_ms;
    
    while (remaining > 0 && 
           running_.load(std::memory_order_acquire) &&
           !stop_requested_.load(std::memory_order_acquire)) {
      auto step = std::min<int64_t>(remaining, chunk_ms);
      std::this_thread::sleep_for(std::chrono::milliseconds(step));
      remaining -= step;
    }
  }
}

bool TsOutputSink::TryAcceptClient() {
  // If already connected, don't accept another
  if (client_connected_.load(std::memory_order_acquire)) {
    return false;
  }

  if (listen_fd_ < 0) {
    return false;
  }

  int new_client_fd = accept(listen_fd_, nullptr, nullptr);

  if (new_client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return false;  // No client waiting
    } else {
      std::cerr << "[TsOutputSink] Accept error: " << strerror(errno) << std::endl;
      return false;
    }
  }

  // Set client socket to blocking mode for atomic packet writes
  int flags = fcntl(new_client_fd, F_GETFL, 0);
  if (flags < 0) {
    std::cerr << "[TsOutputSink] Failed to get socket flags: " 
              << strerror(errno) << std::endl;
    close(new_client_fd);
    return false;
  }
  // Clear O_NONBLOCK flag to make socket blocking
  if (fcntl(new_client_fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
    std::cerr << "[TsOutputSink] Failed to set client socket blocking: " 
              << strerror(errno) << std::endl;
    close(new_client_fd);
    return false;
  }
  
  // Increase send buffer size for better performance
  int send_buf_size = 256 * 1024;  // 256KB send buffer
  if (setsockopt(new_client_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size)) < 0) {
    std::cerr << "[TsOutputSink] Warning: Failed to set SO_SNDBUF: " 
              << strerror(errno) << std::endl;
    // Continue anyway - not critical
  }

  // Store client file descriptor
  client_fd_ = new_client_fd;
  client_connected_.store(true, std::memory_order_release);

  std::cout << "[TsOutputSink] Client connected (ChannelManager)" << std::endl;
  return true;
}

void TsOutputSink::HandleClientDisconnect() {
  if (!client_connected_.load(std::memory_order_acquire)) {
    return;  // Already disconnected
  }

  std::cout << "[TsOutputSink] Client disconnected" << std::endl;

  // Close client socket
  if (client_fd_ >= 0) {
    close(client_fd_);
    client_fd_ = -1;
  }

  // Mark as disconnected
  client_connected_.store(false, std::memory_order_release);
  
  // Note: We don't close the listen socket - it stays open to accept new connections
}

void TsOutputSink::CleanupSocket() {
  // Close client socket if connected
  if (client_fd_ >= 0) {
    close(client_fd_);
    client_fd_ = -1;
  }
  
  // Close listen socket
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
  
  client_connected_.store(false, std::memory_order_release);
  
  // Unlink socket file
  if (!socket_path_.empty() && std::filesystem::exists(socket_path_)) {
    if (unlink(socket_path_.c_str()) < 0) {
      std::cerr << "[TsOutputSink] Warning: Failed to unlink socket file: " 
                << strerror(errno) << std::endl;
    } else {
      std::cout << "[TsOutputSink] Unlinked socket file: " << socket_path_ << std::endl;
    }
  }
}

}  // namespace retrovue::playout_sinks::mpegts

