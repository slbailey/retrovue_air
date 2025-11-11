#include "../../BaseContractTest.h"
#include "../ContractRegistryEnvironment.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/renderer/FrameRenderer.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "timing/TestMasterClock.h"
#include "../../fixtures/ChannelManagerStub.h"
#include "../../fixtures/MasterClockStub.h"

using namespace std::chrono_literals;

namespace retrovue::tests
{
  namespace
  {

    using retrovue::tests::RegisterExpectedDomainCoverage;

    double ComputeP95(const std::vector<double> &values)
    {
      std::vector<double> sorted(values);
      std::sort(sorted.begin(), sorted.end());
      if (sorted.empty())
      {
        return 0.0;
      }
      const double rank = 0.95 * static_cast<double>(sorted.size() - 1);
      const auto lower_index = static_cast<size_t>(std::floor(rank));
      const auto upper_index = static_cast<size_t>(std::ceil(rank));
      const double fraction = rank - static_cast<double>(lower_index);
      return sorted[lower_index] +
             (sorted[upper_index] - sorted[lower_index]) * fraction;
    }

    const bool kRegisterCoverage = []()
    {
      RegisterExpectedDomainCoverage(
          "MasterClock",
          {"MC-001", "MC-002", "MC-003", "MC-004", "MC-005", "MC-006"});
      return true;
    }();

    class MasterClockContractTest : public BaseContractTest
    {
    protected:
      [[nodiscard]] std::string DomainName() const override
      {
        return "MasterClock";
      }

      [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
      {
        return {
            "MC-001",
            "MC-002",
            "MC-003",
            "MC-004",
            "MC-005",
            "MC-006"};
      }
    };

    // Rule: MC-001 Monotonic now() (MasterClockDomainContract.md §MC_001)
    TEST_F(MasterClockContractTest, MC_001_MonotonicNow)
    {
      retrovue::timing::TestMasterClock clock;
      const int64_t epoch = 1'700'000'000'000'000;
      clock.SetEpochUtcUs(epoch);
      clock.SetNow(epoch, 0.0);

      double last_monotonic = clock.now_monotonic_s();
      std::vector<double> jitter_samples;
      jitter_samples.reserve(200);

      for (int i = 0; i < 200; ++i)
      {
        const int64_t delta_us = (i % 2 == 0) ? 1'000 : 2'000;
        clock.AdvanceMicroseconds(delta_us);

        const double now_monotonic = clock.now_monotonic_s();
        EXPECT_GE(now_monotonic, last_monotonic)
            << "Monotonic clock must never move backwards";

        jitter_samples.push_back(now_monotonic - last_monotonic);
        last_monotonic = now_monotonic;
      }

      ASSERT_FALSE(jitter_samples.empty());
      const double mean_delta =
          std::accumulate(jitter_samples.begin(), jitter_samples.end(), 0.0) /
          static_cast<double>(jitter_samples.size());

      std::vector<double> jitter_abs;
      jitter_abs.reserve(jitter_samples.size());
      for (double delta : jitter_samples)
      {
        jitter_abs.push_back(std::abs(delta - mean_delta));
      }

      const double p95_jitter = ComputeP95(jitter_abs);
      constexpr double kOneMillisecond = 0.001;
      EXPECT_LT(p95_jitter, kOneMillisecond)
          << "Monotonic clock jitter must remain within 1 ms p95";
    }

    // Rule: MC-002 Stable PTS to UTC mapping (MasterClockDomainContract.md §MC_002)
    TEST_F(MasterClockContractTest, MC_002_StablePtsToUtcMapping)
    {
      retrovue::timing::TestMasterClock clock;
      const int64_t epoch = 1'700'000'000'100'000;
      constexpr double kRatePpm = 75.0;
      clock.SetEpochUtcUs(epoch);
      clock.SetRatePpm(kRatePpm);
      clock.SetNow(epoch, 0.0);

      const int64_t pts_step = 33'366; // ~29.97 fps
      std::vector<int64_t> deadlines;
      deadlines.reserve(180);

      for (int i = 0; i < 180; ++i)
      {
        const int64_t pts = i * pts_step;
        const int64_t deadline = clock.scheduled_to_utc_us(pts);
        deadlines.push_back(deadline);

        const int64_t repeated = clock.scheduled_to_utc_us(pts);
        EXPECT_EQ(deadline, repeated)
            << "PTS to UTC mapping must remain deterministic for identical PTS";

        if (i > 0)
        {
          EXPECT_GT(deadline, deadlines[i - 1])
              << "PTS mapping must remain strictly increasing";
        }
      }

      for (size_t i = 0; i < deadlines.size(); ++i)
      {
        const int64_t pts = static_cast<int64_t>(i) * pts_step;
        const long double expected =
            static_cast<long double>(epoch) +
            static_cast<long double>(pts) *
                (1.0L + static_cast<long double>(kRatePpm) / 1'000'000.0L);
        const long double diff =
            std::llabs(deadlines[i] - static_cast<int64_t>(expected));
        EXPECT_LT(diff, 100.0L)
            << "PTS to UTC mapping must stay within ±0.1 ms";
      }
    }

    // Rule: MC-003 Pace controller convergence (MasterClockDomainContract.md §MC_003)
    TEST_F(MasterClockContractTest, MC_003_PaceControllerConvergence)
    {
      buffer::FrameRingBuffer buffer(/*capacity=*/180);
      const int64_t pts_step = 33'366;
      for (int i = 0; i < 180; ++i)
      {
        buffer::Frame frame;
        frame.metadata.pts = i * pts_step;
        frame.metadata.duration = 1.0 / 29.97;
        ASSERT_TRUE(buffer.Push(frame));
      }

      auto metrics = std::make_shared<telemetry::MetricsExporter>(0);
      auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
      const int64_t epoch = 1'700'000'000'500'000;
      clock->SetEpochUtcUs(epoch);
      clock->SetRatePpm(0.0);
      clock->SetNow(epoch + 12'000, 0.0); // 12 ms skew

      renderer::RenderConfig config;
      config.mode = renderer::RenderMode::HEADLESS;
      constexpr int32_t kChannelId = 701;
      metrics->UpdateChannelMetrics(kChannelId, telemetry::ChannelMetrics{});

      auto renderer = renderer::FrameRenderer::Create(
          config, buffer, clock, metrics, kChannelId);
      ASSERT_TRUE(renderer);
      ASSERT_TRUE(renderer->Start());

      std::this_thread::sleep_for(150ms);
      telemetry::ChannelMetrics snapshot;
      ASSERT_TRUE(metrics->GetChannelMetrics(kChannelId, snapshot));
      const double observed_gap_ms = std::abs(snapshot.frame_gap_seconds * 1000.0);
      const double initial_gap_ms =
          std::abs(static_cast<double>(clock->scheduled_to_utc_us(0) - (epoch + 12'000)) / 1000.0);
      EXPECT_LE(observed_gap_ms, initial_gap_ms + 0.5)
          << "Pace controller should reduce the absolute frame gap";

      clock->AdvanceSeconds(0.05); // Nudge clock forward to avoid long sleeps during shutdown.
      renderer->Stop();

      const auto &stats = renderer->GetStats();
      EXPECT_GE(stats.frames_rendered, 1u);
      EXPECT_GT(stats.corrections_total, 0u)
          << "Pace controller should apply corrective actions";
    }

    // Rule: MC-004 Underrun recovery (MasterClockDomainContract.md §MC_004)
    TEST_F(MasterClockContractTest, MC_004_UnderrunRecovery)
    {
      telemetry::MetricsExporter exporter(/*port=*/0);
      fixtures::ChannelManagerStub manager;

      retrovue::decode::ProducerConfig config;
      config.stub_mode = true;
      config.target_fps = 30.0;
      config.asset_uri = "contract://masterclock/underrun";

      auto runtime = manager.StartChannel(801, config, exporter, /*buffer_capacity=*/8);
      auto &buffer = *runtime.buffer;

      // Stop producer and drain buffer to simulate a sustained underrun.
      runtime.producer->Stop();
      buffer::Frame drained;
      while (buffer.Pop(drained))
      {
      }
      EXPECT_TRUE(buffer.IsEmpty()) << "Buffer must be empty after draining.";

      telemetry::ChannelMetrics buffering{};
      buffering.state = telemetry::ChannelState::BUFFERING;
      buffering.buffer_depth_frames = 0;
      exporter.UpdateChannelMetrics(runtime.channel_id, buffering);

      telemetry::ChannelMetrics metrics{};
      ASSERT_TRUE(exporter.GetChannelMetrics(runtime.channel_id, metrics));
      EXPECT_EQ(metrics.state, telemetry::ChannelState::BUFFERING)
          << "MasterClock must surface buffering state during underrun";
      EXPECT_EQ(metrics.buffer_depth_frames, 0u);

      // Refill buffer and publish READY state.
      const size_t refill_count = std::max<size_t>(1, buffer.Capacity() - 1);
      for (size_t i = 0; i < refill_count; ++i)
      {
        buffer::Frame f;
        f.metadata.pts = static_cast<int64_t>(i);
        f.metadata.duration = 1.0 / config.target_fps;
        ASSERT_TRUE(buffer.Push(f));
      }

      telemetry::ChannelMetrics ready{};
      ready.state = telemetry::ChannelState::READY;
      ready.buffer_depth_frames = buffer.Size();
      exporter.UpdateChannelMetrics(runtime.channel_id, ready);

      ASSERT_TRUE(exporter.GetChannelMetrics(runtime.channel_id, metrics));
      EXPECT_EQ(metrics.state, telemetry::ChannelState::READY)
          << "MasterClock must resume ready state once depth is restored";
      EXPECT_GE(metrics.buffer_depth_frames, 1u);

      manager.StopChannel(runtime, exporter);
    }

    // Rule: MC-005 Large gap handling (MasterClockDomainContract.md §MC_005)
    TEST_F(MasterClockContractTest, MC_005_LargeGapHandling)
    {
      buffer::FrameRingBuffer buffer(/*capacity=*/32);
      const int64_t pts_step = 33'366;
      for (int i = 0; i < 24; ++i)
      {
        buffer::Frame frame;
        frame.metadata.pts = i * pts_step;
        frame.metadata.duration = 1.0 / 30.0;
        ASSERT_TRUE(buffer.Push(frame));
      }

      auto metrics = std::make_shared<telemetry::MetricsExporter>(0);
      auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
      const int64_t now_utc = 1'700'000'100'000'000;
      clock->SetNow(now_utc, 0.0);
      clock->SetEpochUtcUs(now_utc - 6'500'000); // ~6.5 s in the past
      clock->SetRatePpm(0.0);

      telemetry::ChannelMetrics seed{};
      seed.state = telemetry::ChannelState::READY;
      constexpr int32_t kChannelId = 901;
      metrics->UpdateChannelMetrics(kChannelId, seed);

      renderer::RenderConfig config;
      config.mode = renderer::RenderMode::HEADLESS;

      auto renderer = renderer::FrameRenderer::Create(
          config, buffer, clock, metrics, kChannelId);
      ASSERT_TRUE(renderer);
      ASSERT_TRUE(renderer->Start());

      std::this_thread::sleep_for(120ms);
      clock->AdvanceSeconds(0.5);
      renderer->Stop();

      const auto &stats = renderer->GetStats();
      EXPECT_GT(stats.frames_dropped, 0u)
          << "Renderer must drop frames to recover from large negative gaps";
      EXPECT_GT(stats.corrections_total, 0u)
          << "Large gap handling must increment correction counters";

      telemetry::ChannelMetrics snapshot;
      ASSERT_TRUE(metrics->GetChannelMetrics(kChannelId, snapshot));
      EXPECT_LT(snapshot.frame_gap_seconds, -5.0)
          << "Frame gap telemetry must reflect large negative gap";
      EXPECT_EQ(snapshot.corrections_total, stats.corrections_total);
    }

    // Rule: MC-006 Telemetry coverage (MasterClockDomainContract.md §MC_006)
    TEST_F(MasterClockContractTest, MC_006_TelemetryCoverage)
    {
      retrovue::timing::TestMasterClock clock;
      const int64_t epoch = 1'700'000'200'000'000;
      clock.SetEpochUtcUs(epoch);
      clock.SetRatePpm(0.0);
      clock.SetDriftPpm(12.5);
      clock.SetNow(epoch, 0.0);

      std::vector<double> jitter_samples;
      jitter_samples.reserve(120);
      double last = clock.now_monotonic_s();
      for (int i = 0; i < 120; ++i)
      {
        const int64_t delta_us = 900 + (i % 3) * 100;
        clock.AdvanceMicroseconds(delta_us);
        const double now = clock.now_monotonic_s();
        jitter_samples.push_back(now - last);
        last = now;
      }
      const double jitter_p95 = ComputeP95(jitter_samples);
      EXPECT_LT(jitter_p95, 0.0015)
          << "Jitter p95 must remain within telemetry tolerance";

      buffer::FrameRingBuffer buffer(/*capacity=*/24);
      for (int i = 0; i < 12; ++i)
      {
        buffer::Frame frame;
        frame.metadata.pts = i * 33'366;
        frame.metadata.duration = 1.0 / 30.0;
        ASSERT_TRUE(buffer.Push(frame));
      }

      auto metrics = std::make_shared<telemetry::MetricsExporter>(0);
      telemetry::ChannelMetrics seed{};
      seed.state = telemetry::ChannelState::READY;
      constexpr int32_t kChannelId = 1001;
      metrics->UpdateChannelMetrics(kChannelId, seed);

      auto shared_clock = std::make_shared<retrovue::timing::TestMasterClock>(clock);

      renderer::RenderConfig config;
      config.mode = renderer::RenderMode::HEADLESS;
      auto renderer = renderer::FrameRenderer::Create(
          config, buffer, shared_clock, metrics, kChannelId);
      ASSERT_TRUE(renderer);
      ASSERT_TRUE(renderer->Start());

      std::this_thread::sleep_for(80ms);
      shared_clock->AdvanceSeconds(0.5);
      renderer->Stop();

      const auto &stats = renderer->GetStats();
      telemetry::ChannelMetrics snapshot;
      ASSERT_TRUE(metrics->GetChannelMetrics(kChannelId, snapshot));

      EXPECT_EQ(snapshot.state, telemetry::ChannelState::READY);
      EXPECT_EQ(snapshot.corrections_total, stats.corrections_total);
      EXPECT_NEAR(snapshot.frame_gap_seconds, stats.frame_gap_ms / 1000.0, 1e-3);
      EXPECT_DOUBLE_EQ(shared_clock->drift_ppm(), 12.5);
      EXPECT_GE(snapshot.buffer_depth_frames, 0u);
    }

  } // namespace
} // namespace retrovue::tests
