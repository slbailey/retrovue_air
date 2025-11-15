// Repository: Retrovue-air
// Component: TS Output Sink
// Purpose: Unix Domain Socket sink for MPEG-TS stream output.
// Copyright (c) 2025 RetroVue

#ifndef RETROVUE_PLAYOUT_SINKS_MPEGTS_TS_OUTPUT_SINK_H_
#define RETROVUE_PLAYOUT_SINKS_MPEGTS_TS_OUTPUT_SINK_H_

#include <cstdint>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

namespace retrovue::playout_sinks::mpegts {

// TsOutputSink wraps a Unix Domain Socket (AF_UNIX, SOCK_STREAM) for outputting
// MPEG-TS packets. Air acts as the server (binds/listens), ChannelManager connects as client.
class TsOutputSink {
 public:
  // Constructs a TS output sink with the given socket path.
  // socket_path: Path to Unix domain socket (e.g., /var/run/retrovue/air/channel_1.sock)
  explicit TsOutputSink(const std::string& socket_path);
  
  ~TsOutputSink();

  // Disable copy and move
  TsOutputSink(const TsOutputSink&) = delete;
  TsOutputSink& operator=(const TsOutputSink&) = delete;
  TsOutputSink(TsOutputSink&&) = delete;
  TsOutputSink& operator=(TsOutputSink&&) = delete;

  // Initialize the socket server (bind, listen).
  // Returns true on success, false on failure.
  // If socket file exists, it will be unlinked first.
  bool Initialize();

  // Start accepting client connections in a background thread.
  // Returns true on success, false on failure.
  bool Start();

  // Stop accepting connections and close sockets.
  void Stop();

  // Write TS data to the connected client.
  // data: Pointer to TS packet data
  // size: Number of bytes to write
  // Returns true if all bytes were written, false on error or no client connected.
  // This method is thread-safe and uses blocking socket (ensures atomic packet writes).
  bool Write(const uint8_t* data, size_t size);

  // Check if a client is currently connected.
  bool IsClientConnected() const;

  // Get the socket path.
  const std::string& GetSocketPath() const { return socket_path_; }

 private:
  // Accept thread function (handles client connections).
  void AcceptThread();

  // Try to accept a new client connection (non-blocking).
  // Returns true if client connected, false otherwise.
  bool TryAcceptClient();

  // Handle client disconnect (cleanup and prepare for reconnect).
  void HandleClientDisconnect();

  // Cleanup socket resources.
  void CleanupSocket();

  std::string socket_path_;
  int listen_fd_;
  int client_fd_;
  std::atomic<bool> client_connected_;
  std::atomic<bool> running_;
  std::atomic<bool> stop_requested_;
  std::thread accept_thread_;
  mutable std::mutex state_mutex_;
};

}  // namespace retrovue::playout_sinks::mpegts

#endif  // RETROVUE_PLAYOUT_SINKS_MPEGTS_TS_OUTPUT_SINK_H_

