// Repository: Retrovue-playout
// Component: Playout Controller
// Purpose: High-level controller that orchestrates channel lifecycle operations.
// Copyright (c) 2025 RetroVue

#ifndef RETROVUE_RUNTIME_PLAYOUT_CONTROLLER_H_
#define RETROVUE_RUNTIME_PLAYOUT_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <optional>

namespace retrovue::runtime {

// Forward declaration
class PlayoutEngine;

// Result structure for controller operations
struct ControllerResult {
  bool success;
  std::string message;
  
  // For LoadPreview
  bool shadow_decode_started = false;
  
  // For SwitchToLive
  bool pts_contiguous = false;
  uint64_t live_start_pts = 0;
  
  ControllerResult(bool s, const std::string& msg)
      : success(s), message(msg) {}
};

// PlayoutController is a thin adapter between gRPC and the domain engine.
// It delegates all operations to PlayoutEngine which contains the tested domain logic.
class PlayoutController {
 public:
  // Constructs controller with a reference to the domain engine
  explicit PlayoutController(std::shared_ptr<PlayoutEngine> engine);
  
  ~PlayoutController();
  
  // Disable copy and move
  PlayoutController(const PlayoutController&) = delete;
  PlayoutController& operator=(const PlayoutController&) = delete;
  
  // Start a new channel with the given configuration
  ControllerResult StartChannel(
      int32_t channel_id,
      const std::string& plan_handle,
      int32_t port,
      const std::optional<std::string>& uds_path = std::nullopt);
  
  // Stop a channel gracefully
  ControllerResult StopChannel(int32_t channel_id);
  
  // Load a preview asset into shadow decode mode
  ControllerResult LoadPreview(
      int32_t channel_id,
      const std::string& asset_path);
  
  // Switch preview slot to live atomically
  ControllerResult SwitchToLive(int32_t channel_id);
  
  // Update the playout plan for an active channel
  ControllerResult UpdatePlan(
      int32_t channel_id,
      const std::string& plan_handle);
  
 private:
  // Domain engine that contains the tested implementation
  std::shared_ptr<PlayoutEngine> engine_;
};

}  // namespace retrovue::runtime

#endif  // RETROVUE_RUNTIME_PLAYOUT_CONTROLLER_H_

