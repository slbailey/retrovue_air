// Repository: Retrovue-playout
// Component: Frame Producer
// Purpose: Decodes media assets and produces frames for the ring buffer.
// Copyright (c) 2025 RetroVue

#include "retrovue/decode/FrameProducer.h"
#include "retrovue/decode/FFmpegDecoder.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace retrovue::decode {

FrameProducer::FrameProducer(const ProducerConfig& config,
                             buffer::FrameRingBuffer& output_buffer)
    : config_(config),
      output_buffer_(output_buffer),
      running_(false),
      stop_requested_(false),
      frames_produced_(0),
      buffer_full_count_(0),
      stub_pts_counter_(0) {
}

FrameProducer::~FrameProducer() {
  Stop();
}

bool FrameProducer::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return false;  // Already running
  }

  stop_requested_.store(false, std::memory_order_release);
  producer_thread_ = std::make_unique<std::thread>(&FrameProducer::ProduceLoop, this);

  std::cout << "[FrameProducer] Started for asset: " << config_.asset_uri << std::endl;
  return true;
}

void FrameProducer::Stop() {
  if (!running_.load(std::memory_order_acquire)) {
    return;  // Not running
  }

  std::cout << "[FrameProducer] Stopping..." << std::endl;
  stop_requested_.store(true, std::memory_order_release);

  if (producer_thread_ && producer_thread_->joinable()) {
    producer_thread_->join();
  }

  running_.store(false, std::memory_order_release);
  std::cout << "[FrameProducer] Stopped. Total frames produced: " 
            << frames_produced_.load() << std::endl;
}

void FrameProducer::ProduceLoop() {
  std::cout << "[FrameProducer] Decode loop started (stub_mode=" 
            << (config_.stub_mode ? "true" : "false") << ")" << std::endl;

  // Initialize decoder if not in stub mode
  if (!config_.stub_mode) {
    DecoderConfig decoder_config;
    decoder_config.input_uri = config_.asset_uri;
    decoder_config.target_width = config_.target_width;
    decoder_config.target_height = config_.target_height;
    decoder_config.hw_accel_enabled = config_.hw_accel_enabled;
    decoder_config.max_decode_threads = config_.max_decode_threads;

    decoder_ = std::make_unique<FFmpegDecoder>(decoder_config);
    
    if (!decoder_->Open()) {
      std::cerr << "[FrameProducer] Failed to open decoder, falling back to stub mode" 
                << std::endl;
      config_.stub_mode = true;  // Fallback to stub mode
      decoder_.reset();
    } else {
      std::cout << "[FrameProducer] FFmpeg decoder initialized successfully" << std::endl;
    }
  }

  // Calculate frame interval based on target FPS (for stub mode)
  const auto frame_interval = std::chrono::microseconds(
      static_cast<int64_t>(1000000.0 / config_.target_fps));

  while (!stop_requested_.load(std::memory_order_acquire)) {
    auto frame_start = std::chrono::steady_clock::now();

    if (config_.stub_mode) {
      ProduceStubFrame();
      
      // Sleep to maintain target frame rate in stub mode
      auto frame_end = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          frame_end - frame_start);
      
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      }
    } else {
      ProduceRealFrame();
      // No artificial delay needed - real decode has its own timing
    }
  }

  // Cleanup decoder
  if (decoder_) {
    decoder_->Close();
    decoder_.reset();
  }

  std::cout << "[FrameProducer] Decode loop exited" << std::endl;
}

void FrameProducer::ProduceStubFrame() {
  // Create a stub frame with synthetic data
  buffer::Frame frame;
  
  // Set metadata
  frame.metadata.pts = stub_pts_counter_;
  frame.metadata.dts = stub_pts_counter_;
  frame.metadata.duration = 1.0 / config_.target_fps;
  frame.metadata.asset_uri = config_.asset_uri;
  
  // Set dimensions
  frame.width = config_.target_width;
  frame.height = config_.target_height;
  
  // Generate stub YUV420 data (simple gradient pattern)
  // Y plane: full resolution
  // U plane: half resolution
  // V plane: half resolution
  const size_t y_size = frame.width * frame.height;
  const size_t uv_size = (frame.width / 2) * (frame.height / 2);
  const size_t total_size = y_size + 2 * uv_size;
  
  frame.data.resize(total_size);
  
  // Fill with a simple pattern based on frame number
  // Y plane: gradient based on PTS
  uint8_t y_value = static_cast<uint8_t>((stub_pts_counter_ * 10) % 256);
  std::fill(frame.data.begin(), frame.data.begin() + y_size, y_value);
  
  // U and V planes: constant gray
  std::fill(frame.data.begin() + y_size, frame.data.end(), 128);
  
  // Try to push frame into buffer
  if (output_buffer_.Push(frame)) {
    frames_produced_.fetch_add(1, std::memory_order_relaxed);
    stub_pts_counter_++;
  } else {
    // Buffer full - back off slightly
    buffer_full_count_.fetch_add(1, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void FrameProducer::ProduceRealFrame() {
  if (!decoder_ || !decoder_->IsOpen()) {
    std::cerr << "[FrameProducer] Decoder not available" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return;
  }

  // Decode next frame
  if (!decoder_->DecodeNextFrame(output_buffer_)) {
    if (decoder_->IsEOF()) {
      std::cout << "[FrameProducer] End of file reached" << std::endl;
      stop_requested_.store(true, std::memory_order_release);
    } else {
      // Decode error or buffer full
      const auto& stats = decoder_->GetStats();
      if (stats.decode_errors > 0) {
        std::cerr << "[FrameProducer] Decode errors: " << stats.decode_errors << std::endl;
      }
      
      // Back off slightly on errors or full buffer
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      buffer_full_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  // Frame successfully decoded and pushed
  frames_produced_.fetch_add(1, std::memory_order_relaxed);

  // Log progress periodically
  const auto& stats = decoder_->GetStats();
  if (stats.frames_decoded % 100 == 0) {
    std::cout << "[FrameProducer] Decoded " << stats.frames_decoded 
              << " frames, avg decode time: " << stats.average_decode_time_ms << "ms, "
              << "current fps: " << stats.current_fps << std::endl;
  }
}

}  // namespace retrovue::decode

