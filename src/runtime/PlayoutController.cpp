// Repository: Retrovue-playout
// Component: Playout Controller Implementation
// Purpose: High-level controller that orchestrates channel lifecycle operations.
// Copyright (c) 2025 RetroVue

#include "retrovue/runtime/PlayoutController.h"
#include "retrovue/runtime/PlayoutEngine.h"

namespace retrovue::runtime {

PlayoutController::PlayoutController(std::shared_ptr<PlayoutEngine> engine)
    : engine_(std::move(engine)) {
}

PlayoutController::~PlayoutController() = default;

ControllerResult PlayoutController::StartChannel(
    int32_t channel_id,
    const std::string& plan_handle,
    int32_t port,
    const std::optional<std::string>& uds_path) {
  // Delegate to domain engine
  auto result = engine_->StartChannel(channel_id, plan_handle, port, uds_path);
  ControllerResult controller_result(result.success, result.message);
  return controller_result;
}

ControllerResult PlayoutController::StopChannel(int32_t channel_id) {
  // Delegate to domain engine
  auto result = engine_->StopChannel(channel_id);
  return ControllerResult(result.success, result.message);
}

ControllerResult PlayoutController::LoadPreview(
    int32_t channel_id,
    const std::string& asset_path) {
  // Delegate to domain engine
  auto result = engine_->LoadPreview(channel_id, asset_path);
  ControllerResult controller_result(result.success, result.message);
  controller_result.shadow_decode_started = result.shadow_decode_started;
  return controller_result;
}

ControllerResult PlayoutController::SwitchToLive(int32_t channel_id) {
  // Delegate to domain engine
  auto result = engine_->SwitchToLive(channel_id);
  ControllerResult controller_result(result.success, result.message);
  controller_result.pts_contiguous = result.pts_contiguous;
  controller_result.live_start_pts = result.live_start_pts;
  return controller_result;
}

ControllerResult PlayoutController::UpdatePlan(
    int32_t channel_id,
    const std::string& plan_handle) {
  // Delegate to domain engine
  auto result = engine_->UpdatePlan(channel_id, plan_handle);
  return ControllerResult(result.success, result.message);
}

}  // namespace retrovue::runtime

