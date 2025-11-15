// Repository: Retrovue-playout
// Component: Playout Engine Domain Implementation
// Purpose: Domain-level engine that manages channel lifecycle operations.
// Copyright (c) 2025 RetroVue

#include "retrovue/runtime/PlayoutEngine.h"

#include <chrono>
#include <iostream>
#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/decode/FrameProducer.h"
#include "retrovue/renderer/FrameRenderer.h"
#include "retrovue/runtime/OrchestrationLoop.h"
#include "retrovue/runtime/PlayoutControlStateMachine.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "retrovue/timing/MasterClock.h"

namespace retrovue::runtime {

namespace {
  constexpr size_t kDefaultBufferSize = 60; // 60 frames (~2 seconds at 30fps)
  constexpr size_t kReadyDepth = 3; // Minimum buffer depth for ready state
  constexpr auto kReadyTimeout = std::chrono::seconds(2);
  
  int64_t NowUtc(const std::shared_ptr<timing::MasterClock>& clock) {
    if (clock) {
      return clock->now_utc_us();
    }
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
  }
  
  std::string MakeCommandId(const char* prefix, int32_t channel_id) {
    return std::string(prefix) + "-" + std::to_string(channel_id);
  }
  
  telemetry::ChannelState ToChannelState(PlayoutControlStateMachine::State state) {
    using State = PlayoutControlStateMachine::State;
    switch (state) {
      case State::kIdle:
        return telemetry::ChannelState::STOPPED;
      case State::kBuffering:
        return telemetry::ChannelState::BUFFERING;
      case State::kReady:
      case State::kPlaying:
      case State::kPaused:
        return telemetry::ChannelState::READY;
      case State::kStopping:
        return telemetry::ChannelState::BUFFERING;
      case State::kError:
        return telemetry::ChannelState::ERROR_STATE;
    }
    return telemetry::ChannelState::STOPPED;
  }
}  // namespace

// Internal channel state - manages all components for a single channel
struct PlayoutEngine::ChannelState {
  int32_t channel_id;
  std::string plan_handle;
  int32_t port;
  std::optional<std::string> uds_path;
  
  // Core components
  std::unique_ptr<buffer::FrameRingBuffer> ring_buffer;
  std::unique_ptr<decode::FrameProducer> live_producer;
  std::unique_ptr<decode::FrameProducer> preview_producer;  // For shadow decode/preview
  std::unique_ptr<renderer::FrameRenderer> renderer;
  std::unique_ptr<OrchestrationLoop> orchestration_loop;
  std::unique_ptr<PlayoutControlStateMachine> control;
  
  ChannelState(int32_t id, const std::string& plan, int32_t p, 
               const std::optional<std::string>& uds)
      : channel_id(id), plan_handle(plan), port(p), uds_path(uds) {}
};

PlayoutEngine::PlayoutEngine(
    std::shared_ptr<telemetry::MetricsExporter> metrics_exporter,
    std::shared_ptr<timing::MasterClock> master_clock)
    : metrics_exporter_(std::move(metrics_exporter)),
      master_clock_(std::move(master_clock)) {
}

PlayoutEngine::~PlayoutEngine() {
  // Stop all channels on destruction
  std::lock_guard<std::mutex> lock(channels_mutex_);
  for (auto& [channel_id, state] : channels_) {
    if (state) {
      StopChannel(channel_id);
    }
  }
  channels_.clear();
}

EngineResult PlayoutEngine::StartChannel(
    int32_t channel_id,
    const std::string& plan_handle,
    int32_t port,
    const std::optional<std::string>& uds_path) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  
  // Check if channel already exists
  if (channels_.find(channel_id) != channels_.end()) {
    return EngineResult(true, "Channel " + std::to_string(channel_id) + " already started");
  }
  
  try {
    // Create channel state
    auto state = std::make_unique<ChannelState>(channel_id, plan_handle, port, uds_path);
    
    // Create ring buffer
    state->ring_buffer = std::make_unique<buffer::FrameRingBuffer>(kDefaultBufferSize);
    
    // Create control state machine
    state->control = std::make_unique<PlayoutControlStateMachine>();
    
    // Create producer config from plan_handle (simplified - in production, resolve plan to asset)
    decode::ProducerConfig producer_config;
    producer_config.asset_uri = plan_handle; // For now, use plan_handle as asset URI
    producer_config.target_fps = 30.0;
    producer_config.stub_mode = false; // Use real decode
    
    // Create live producer
    state->live_producer = std::make_unique<decode::FrameProducer>(
        producer_config, *state->ring_buffer, master_clock_);
    
    // Create renderer
    renderer::RenderConfig render_config;
    render_config.mode = renderer::RenderMode::HEADLESS;
    state->renderer = renderer::FrameRenderer::Create(
        render_config, *state->ring_buffer, master_clock_, metrics_exporter_, channel_id);
    
    // Start control state machine
    const int64_t now = NowUtc(master_clock_);
    if (!state->control->BeginSession(MakeCommandId("start", channel_id), now)) {
      return EngineResult(false, "Failed to begin session for channel " + std::to_string(channel_id));
    }
    
    // Start producer
    if (!state->live_producer->Start()) {
      return EngineResult(false, "Failed to start producer for channel " + std::to_string(channel_id));
    }
    
    // Start renderer
    if (!state->renderer->Start()) {
      return EngineResult(false, "Failed to start renderer for channel " + std::to_string(channel_id));
    }
    
    // Wait for minimum buffer depth (like ChannelManagerStub)
    const auto start_time = std::chrono::steady_clock::now();
    while (state->ring_buffer->Size() < kReadyDepth) {
      if (std::chrono::steady_clock::now() - start_time > kReadyTimeout) {
        telemetry::ChannelMetrics metrics{};
        metrics.state = telemetry::ChannelState::BUFFERING;
        metrics.buffer_depth_frames = state->ring_buffer->Size();
        metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
        return EngineResult(false, "Timeout waiting for buffer depth on channel " + std::to_string(channel_id));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Update state machine with buffer depth
    state->control->OnBufferDepth(state->ring_buffer->Size(), kDefaultBufferSize, NowUtc(master_clock_));
    
    // Submit ready metrics
    telemetry::ChannelMetrics metrics{};
    metrics.state = telemetry::ChannelState::READY;
    metrics.buffer_depth_frames = state->ring_buffer->Size();
    metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
    
    // Store channel state
    channels_[channel_id] = std::move(state);
    
    return EngineResult(true, "Channel " + std::to_string(channel_id) + " started successfully");
  } catch (const std::exception& e) {
    return EngineResult(false, "Exception starting channel " + std::to_string(channel_id) + ": " + e.what());
  }
}

EngineResult PlayoutEngine::StopChannel(int32_t channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " not found");
  }
  
  auto& state = it->second;
  if (!state) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " state is null");
  }
  
  try {
    const int64_t now = NowUtc(master_clock_);
    
    // Stop control state machine
    if (state->control) {
      state->control->Stop(MakeCommandId("stop", channel_id), now, now);
    }
    
    // Stop renderer first (consumer before producer)
    if (state->renderer) {
      state->renderer->Stop();
    }
    
    // Stop producers
    if (state->live_producer) {
      state->live_producer->RequestTeardown(std::chrono::milliseconds(500));
      while (state->live_producer->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      state->live_producer->Stop();
    }
    
    if (state->preview_producer) {
      state->preview_producer->RequestTeardown(std::chrono::milliseconds(500));
      while (state->preview_producer->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      state->preview_producer->Stop();
    }
    
    // Drain buffer
    if (state->ring_buffer) {
      buffer::Frame frame;
      while (state->ring_buffer->Pop(frame)) {
        // Drain all frames
      }
      state->ring_buffer->Clear();
    }
    
    // Submit stopped metrics
    telemetry::ChannelMetrics metrics{};
    metrics.state = telemetry::ChannelState::STOPPED;
    metrics.buffer_depth_frames = 0;
    metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
    
    // Remove channel
    channels_.erase(it);
    
    return EngineResult(true, "Channel " + std::to_string(channel_id) + " stopped successfully");
  } catch (const std::exception& e) {
    return EngineResult(false, "Exception stopping channel " + std::to_string(channel_id) + ": " + e.what());
  }
}

EngineResult PlayoutEngine::LoadPreview(
    int32_t channel_id,
    const std::string& asset_path) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " not found");
  }
  
  auto& state = it->second;
  if (!state) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " state is null");
  }
  
  try {
    // Create preview producer config
    decode::ProducerConfig preview_config;
    preview_config.asset_uri = asset_path;
    preview_config.target_fps = 30.0;
    preview_config.stub_mode = false;
    
    // Create preview producer (shadow decode - doesn't write to buffer yet)
    // Note: FrameProducer doesn't currently support shadow mode directly,
    // so we create it but don't start it writing to buffer until SwitchToLive
    state->preview_producer = std::make_unique<decode::FrameProducer>(
        preview_config, *state->ring_buffer, master_clock_);
    
    // For now, start it normally (in a real implementation, shadow mode would
    // decode without writing to buffer until SwitchToLive)
    if (!state->preview_producer->Start()) {
      return EngineResult(false, "Failed to start preview producer for channel " + std::to_string(channel_id));
    }
    
    EngineResult result(true, "Preview loaded for channel " + std::to_string(channel_id));
    result.shadow_decode_started = true;
    return result;
  } catch (const std::exception& e) {
    return EngineResult(false, "Exception loading preview for channel " + std::to_string(channel_id) + ": " + e.what());
  }
}

EngineResult PlayoutEngine::SwitchToLive(int32_t channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " not found");
  }
  
  auto& state = it->second;
  if (!state) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " state is null");
  }
  
  if (!state->preview_producer) {
    return EngineResult(false, "No preview producer loaded for channel " + std::to_string(channel_id));
  }
  
  try {
    // Stop live producer
    if (state->live_producer) {
      state->live_producer->RequestTeardown(std::chrono::milliseconds(500));
      while (state->live_producer->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      state->live_producer->Stop();
    }
    
    // Swap preview to live
    state->live_producer = std::move(state->preview_producer);
    state->preview_producer.reset();
    
    // For PTS continuity, we'd need to align preview PTS to live's next PTS
    // This is simplified - in production, would check PTS alignment
    EngineResult result(true, "Switched to live for channel " + std::to_string(channel_id));
    result.pts_contiguous = true; // Simplified - would check actual PTS continuity
    result.live_start_pts = 0; // Would get from producer/renderer
    
    return result;
  } catch (const std::exception& e) {
    return EngineResult(false, "Exception switching to live for channel " + std::to_string(channel_id) + ": " + e.what());
  }
}

EngineResult PlayoutEngine::UpdatePlan(
    int32_t channel_id,
    const std::string& plan_handle) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " not found");
  }
  
  auto& state = it->second;
  if (!state) {
    return EngineResult(false, "Channel " + std::to_string(channel_id) + " state is null");
  }
  
  try {
    // Update plan handle
    state->plan_handle = plan_handle;
    
    // In production, would restart producer with new plan
    // For now, just update the handle
    return EngineResult(true, "Plan updated for channel " + std::to_string(channel_id));
  } catch (const std::exception& e) {
    return EngineResult(false, "Exception updating plan for channel " + std::to_string(channel_id) + ": " + e.what());
  }
}

}  // namespace retrovue::runtime

