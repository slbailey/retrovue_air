// Repository: Retrovue-playout
// Component: Frame Producer Unit Tests
// Purpose: Tests decode thread lifecycle and frame production.
// Copyright (c) 2025 RetroVue

#include "retrovue/decode/FrameProducer.h"
#include "retrovue/buffer/FrameRingBuffer.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace retrovue::decode;
using namespace retrovue::buffer;

// Test producer construction and initial state
TEST(FrameProducerTest, Construction) {
  FrameRingBuffer buffer(60);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  EXPECT_FALSE(producer.IsRunning());
  EXPECT_EQ(producer.GetFramesProduced(), 0);
}

// Test starting and stopping producer
TEST(FrameProducerTest, StartStop) {
  FrameRingBuffer buffer(60);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  EXPECT_TRUE(producer.IsRunning());
  
  // Let it run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  producer.Stop();
  EXPECT_FALSE(producer.IsRunning());
  
  // Should have produced some frames
  EXPECT_GT(producer.GetFramesProduced(), 0);
}

// Test that producer fills buffer
TEST(FrameProducerTest, FillsBuffer) {
  const size_t buffer_size = 10;
  FrameRingBuffer buffer(buffer_size);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.target_fps = 100.0;  // Fast frame rate for testing
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  
  // Wait for buffer to fill
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Buffer should be full or nearly full
  EXPECT_GE(buffer.Size(), buffer_size / 2);
  
  producer.Stop();
}

// Test frame PTS incrementing
TEST(FrameProducerTest, FramePTSIncrementing) {
  FrameRingBuffer buffer(100);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.target_fps = 30.0;
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  
  // Let some frames accumulate
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  producer.Stop();
  
  // Verify PTS values increment
  int64_t last_pts = -1;
  Frame frame;
  
  while (buffer.Pop(frame)) {
    if (last_pts >= 0) {
      EXPECT_EQ(frame.metadata.pts, last_pts + 1);
    }
    last_pts = frame.metadata.pts;
  }
  
  EXPECT_GT(last_pts, 0);  // Should have consumed multiple frames
}

// Test frame metadata
TEST(FrameProducerTest, FrameMetadata) {
  FrameRingBuffer buffer(100);
  
  ProducerConfig config;
  config.asset_uri = "test://my-asset";
  config.target_width = 1920;
  config.target_height = 1080;
  config.target_fps = 30.0;
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  
  // Wait for at least one frame
  while (buffer.IsEmpty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  producer.Stop();
  
  Frame frame;
  ASSERT_TRUE(buffer.Pop(frame));
  
  EXPECT_EQ(frame.metadata.asset_uri, "test://my-asset");
  EXPECT_EQ(frame.width, 1920);
  EXPECT_EQ(frame.height, 1080);
  EXPECT_NEAR(frame.metadata.duration, 1.0 / 30.0, 0.001);
  EXPECT_FALSE(frame.data.empty());
}

// Test cannot start twice
TEST(FrameProducerTest, CannotStartTwice) {
  FrameRingBuffer buffer(60);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  EXPECT_FALSE(producer.Start());  // Second start should fail
  
  producer.Stop();
}

// Test buffer full handling
TEST(FrameProducerTest, BufferFullHandling) {
  const size_t buffer_size = 5;  // Very small buffer
  FrameRingBuffer buffer(buffer_size);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.target_fps = 100.0;  // Fast to fill buffer quickly
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  ASSERT_TRUE(producer.Start());
  
  // Wait for buffer to fill and producer to hit buffer-full condition
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  uint64_t buffer_full_count = producer.GetBufferFullCount();
  
  producer.Stop();
  
  // Producer should have hit buffer-full at least a few times
  EXPECT_GT(buffer_full_count, 0);
}

// Test stop idempotency
TEST(FrameProducerTest, StopIdempotent) {
  FrameRingBuffer buffer(60);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.stub_mode = true;
  
  FrameProducer producer(config, buffer);
  
  producer.Stop();  // Stop before start - should be safe
  EXPECT_FALSE(producer.IsRunning());
  
  ASSERT_TRUE(producer.Start());
  producer.Stop();
  producer.Stop();  // Stop twice - should be safe
  
  EXPECT_FALSE(producer.IsRunning());
}

// Test destructor stops producer
TEST(FrameProducerTest, DestructorStopsProducer) {
  FrameRingBuffer buffer(60);
  
  ProducerConfig config;
  config.asset_uri = "test://asset";
  config.stub_mode = true;
  
  {
    FrameProducer producer(config, buffer);
    ASSERT_TRUE(producer.Start());
    EXPECT_TRUE(producer.IsRunning());
    // Destructor called here
  }
  
  // If we get here without hanging, destructor worked correctly
  SUCCEED();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

