// Repository: Retrovue-playout
// Component: Frame Ring Buffer Unit Tests
// Purpose: Tests ring buffer push/pop, starvation, and overflow logic.
// Copyright (c) 2025 RetroVue

#include "retrovue/buffer/FrameRingBuffer.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace retrovue::buffer;

// Test basic construction and initial state
TEST(FrameRingBufferTest, Construction) {
  FrameRingBuffer buffer(10);
  
  EXPECT_EQ(buffer.Capacity(), 10);
  EXPECT_EQ(buffer.Size(), 0);
  EXPECT_TRUE(buffer.IsEmpty());
  EXPECT_FALSE(buffer.IsFull());
}

// Test single push and pop
TEST(FrameRingBufferTest, SinglePushPop) {
  FrameRingBuffer buffer(10);
  
  Frame frame;
  frame.metadata.pts = 1000;
  frame.metadata.dts = 1000;
  frame.metadata.duration = 0.033;
  frame.metadata.asset_uri = "test://asset";
  frame.width = 1920;
  frame.height = 1080;
  
  ASSERT_TRUE(buffer.Push(frame));
  EXPECT_EQ(buffer.Size(), 1);
  EXPECT_FALSE(buffer.IsEmpty());
  
  Frame popped;
  ASSERT_TRUE(buffer.Pop(popped));
  EXPECT_EQ(buffer.Size(), 0);
  EXPECT_TRUE(buffer.IsEmpty());
  
  EXPECT_EQ(popped.metadata.pts, 1000);
  EXPECT_EQ(popped.metadata.asset_uri, "test://asset");
  EXPECT_EQ(popped.width, 1920);
}

// Test buffer full condition
TEST(FrameRingBufferTest, BufferFull) {
  const size_t capacity = 5;
  FrameRingBuffer buffer(capacity);
  
  // Fill buffer to capacity
  for (size_t i = 0; i < capacity; ++i) {
    Frame frame;
    frame.metadata.pts = static_cast<int64_t>(i);
    ASSERT_TRUE(buffer.Push(frame)) << "Failed to push frame " << i;
  }
  
  EXPECT_TRUE(buffer.IsFull());
  EXPECT_EQ(buffer.Size(), capacity);
  
  // Try to push one more - should fail
  Frame frame;
  EXPECT_FALSE(buffer.Push(frame));
  EXPECT_EQ(buffer.Size(), capacity);
}

// Test buffer wrap-around
TEST(FrameRingBufferTest, WrapAround) {
  const size_t capacity = 5;
  FrameRingBuffer buffer(capacity);
  
  // Fill buffer
  for (size_t i = 0; i < capacity; ++i) {
    Frame frame;
    frame.metadata.pts = static_cast<int64_t>(i);
    ASSERT_TRUE(buffer.Push(frame));
  }
  
  // Pop half
  for (size_t i = 0; i < capacity / 2; ++i) {
    Frame frame;
    ASSERT_TRUE(buffer.Pop(frame));
    EXPECT_EQ(frame.metadata.pts, static_cast<int64_t>(i));
  }
  
  // Push more (causing wrap-around)
  for (size_t i = capacity; i < capacity + capacity / 2; ++i) {
    Frame frame;
    frame.metadata.pts = static_cast<int64_t>(i);
    ASSERT_TRUE(buffer.Push(frame));
  }
  
  // Verify correct order
  size_t expected_pts = capacity / 2;
  while (!buffer.IsEmpty()) {
    Frame frame;
    ASSERT_TRUE(buffer.Pop(frame));
    EXPECT_EQ(frame.metadata.pts, static_cast<int64_t>(expected_pts++));
  }
}

// Test clear operation
TEST(FrameRingBufferTest, Clear) {
  FrameRingBuffer buffer(10);
  
  // Add some frames
  for (int i = 0; i < 5; ++i) {
    Frame frame;
    frame.metadata.pts = i;
    buffer.Push(frame);
  }
  
  EXPECT_EQ(buffer.Size(), 5);
  
  buffer.Clear();
  
  EXPECT_EQ(buffer.Size(), 0);
  EXPECT_TRUE(buffer.IsEmpty());
}

// Test concurrent producer-consumer (stress test)
TEST(FrameRingBufferTest, ConcurrentProducerConsumer) {
  const size_t capacity = 100;
  const int num_frames = 1000;
  FrameRingBuffer buffer(capacity);
  
  std::atomic<bool> producer_done{false};
  std::atomic<int> frames_produced{0};
  std::atomic<int> frames_consumed{0};
  
  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < num_frames; ++i) {
      Frame frame;
      frame.metadata.pts = i;
      
      while (!buffer.Push(frame)) {
        std::this_thread::yield();  // Buffer full, wait
      }
      
      frames_produced.fetch_add(1);
    }
    producer_done.store(true);
  });
  
  // Consumer thread
  std::thread consumer([&]() {
    int expected_pts = 0;
    
    while (frames_consumed.load() < num_frames) {
      Frame frame;
      
      if (buffer.Pop(frame)) {
        EXPECT_EQ(frame.metadata.pts, expected_pts++);
        frames_consumed.fetch_add(1);
      } else {
        std::this_thread::yield();  // Buffer empty, wait
      }
    }
  });
  
  producer.join();
  consumer.join();
  
  EXPECT_EQ(frames_produced.load(), num_frames);
  EXPECT_EQ(frames_consumed.load(), num_frames);
  EXPECT_TRUE(buffer.IsEmpty());
}

// Test pop from empty buffer
TEST(FrameRingBufferTest, PopFromEmpty) {
  FrameRingBuffer buffer(10);
  
  Frame frame;
  EXPECT_FALSE(buffer.Pop(frame));
  EXPECT_TRUE(buffer.IsEmpty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

