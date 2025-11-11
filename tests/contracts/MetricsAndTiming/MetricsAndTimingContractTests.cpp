#include "tests/BaseContractTest.h"
#include "tests/contracts/ContractRegistryEnvironment.h"

#include <cmath>
#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/decode/FrameProducer.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "tests/fixtures/ChannelManagerStub.h"
#include "tests/fixtures/MasterClockStub.h"

using namespace retrovue;
using namespace retrovue::tests;
using namespace retrovue::tests::fixtures;

namespace
{

using retrovue::tests::RegisterExpectedDomainCoverage;

const bool kRegisterCoverage = []() {
  RegisterExpectedDomainCoverage("MetricsAndTiming",
                                 {"MT-001", "MT-002", "MT-003", "MT-004", "MT-005"});
  return true;
}();

class MetricsAndTimingContractTest : public BaseContractTest
{
protected:
  [[nodiscard]] std::string DomainName() const override
  {
    return "MetricsAndTiming";
  }

  [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
  {
    return {
        "MT-001",
        "MT-002",
        "MT-003",
        "MT-004",
        "MT-005"};
  }
};

// Rule: MT-001 Monotonic now() (MasterClockDomainContract.md §MT_001)
TEST_F(MetricsAndTimingContractTest, MT_001_MasterClockMonotonicAndLowJitter)
{
  MasterClockStub clock;
  int64_t last = clock.now_utc_us();
  for (int i = 0; i < 1000; ++i)
  {
    clock.Advance(1'000);  // advance by 1 ms
    const int64_t now = clock.now_utc_us();
    EXPECT_GE(now, last);
    last = now;
  }

  // Deterministic jitter check: advance alternating 1ms/2ms, ensure <1ms p95
  double max_jitter_us = 0.0;
  for (int i = 0; i < 1000; ++i)
  {
    clock.Advance((i % 2 == 0) ? 1'000 : 2'000);
    const int64_t now = clock.now_utc_us();
    const double jitter = std::abs(static_cast<double>(now - last) - 1'500.0);
    max_jitter_us = std::max(max_jitter_us, jitter);
    last = now;
  }
  EXPECT_LT(max_jitter_us, 1'000.0);
}
// Rule: MT-002 Stable PTS mapping (MasterClockDomainContract.md §MT_002)
TEST_F(MetricsAndTimingContractTest, MT_002_PtsToUtcMappingStable)
{
  MasterClockStub clock;
  const int64_t epoch = clock.now_utc_us();
  clock.Advance(0);  // ensure deterministic start

  const double rate_ppm = 100.0;  // small rate offset
  const auto system_clock =
      retrovue::timing::MakeSystemMasterClock(epoch, rate_ppm);

  const int64_t pts_step_us = 33'366;  // approx 29.97 fps
  int64_t previous_deadline = system_clock->scheduled_to_utc_us(0);
  for (int i = 1; i <= 120; ++i)
  {
    const int64_t pts = i * pts_step_us;
    const int64_t deadline = system_clock->scheduled_to_utc_us(pts);
    EXPECT_GT(deadline, previous_deadline);
    const long double expected =
        static_cast<long double>(epoch) +
        static_cast<long double>(pts) * (1.0L + rate_ppm / 1'000'000.0L);
    const long double diff = std::llabs(deadline - static_cast<int64_t>(expected));
    EXPECT_LT(diff, 100.0L) << "PTS conversion must remain stable";
    previous_deadline = deadline;
  }
}

// Rule: MT-003 Pace controller convergence (MasterClockDomainContract.md §MT_003)
TEST_F(MetricsAndTimingContractTest, MT_003_PaceControllerReducesGap)
{
  MasterClockStub clock;
  const int64_t epoch = clock.now_utc_us();

  auto test_clock = std::make_shared<retrovue::timing::TestMasterClock>();
  test_clock->SetEpochUtcUs(epoch);
  test_clock->SetRatePpm(0.0);
  test_clock->SetNow(epoch, 0.0);

  buffer::FrameRingBuffer buffer(120);
  for (int i = 0; i < 120; ++i)
  {
    buffer::Frame frame;
    frame.metadata.pts = i * 33'366;
    buffer.Push(frame);
  }

  retrovue::renderer::RenderConfig config;
  auto metrics = std::make_shared<retrovue::telemetry::MetricsExporter>(0);
  auto renderer = retrovue::renderer::FrameRenderer::Create(
      config, buffer, test_clock, metrics, 7);

  test_clock->SetNow(epoch + 10'000, 0.0);  // inject 10 ms skew

  ASSERT_TRUE(renderer->Start());
  renderer->Stop();

  const auto& stats = renderer->GetStats();
  EXPECT_GE(stats.frames_rendered, 1u);
  EXPECT_LT(std::abs(stats.frame_gap_ms), 8.0);
  EXPECT_GT(stats.corrections_total, 0u);
}

// Rule: MT-004 Underrun recovery (MasterClockDomainContract.md §MT_004)
TEST_F(MetricsAndTimingContractTest, MT_004_UnderrunTriggersBufferingAndRecovery)
{
  telemetry::MetricsExporter exporter(/*port=*/0);
  ChannelManagerStub manager;

  decode::ProducerConfig config;
  config.stub_mode = true;
  config.target_fps = 30.0;
  config.asset_uri = "contract://metrics/underrun";

  auto runtime = manager.StartChannel(303, config, exporter, /*buffer_capacity=*/8);

  buffer::Frame frame;
  auto& buffer = runtime->RingBuffer();
  buffer.Pop(frame);  // induce underrun

  telemetry::ChannelMetrics metrics{};
  ASSERT_TRUE(exporter.GetChannelMetrics(303, metrics));
  EXPECT_EQ(metrics.state, telemetry::ChannelState::BUFFERING);

  // Restore depth
  for (int i = 0; i < 10; ++i)
  {
    buffer::Frame f;
    f.metadata.pts = i;
    buffer.Push(f);
  }

  ASSERT_TRUE(exporter.GetChannelMetrics(303, metrics));
  EXPECT_EQ(metrics.state, telemetry::ChannelState::READY);

  manager.StopChannel(runtime, exporter);
}


// Rule: MT-003 Frame Cadence (MetricsAndTimingContract.md §MT-003)
TEST_F(MetricsAndTimingContractTest, MT_003_FrameCadenceMaintainsMonotonicPts)
{
  buffer::FrameRingBuffer buffer(12);
  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://metrics/frame_cadence";
  config.target_fps = 30.0;

  decode::FrameProducer producer(config, buffer);
  ASSERT_TRUE(producer.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  producer.Stop();

  EXPECT_GE(producer.GetFramesProduced(), 3u);

  buffer::Frame previous;
  bool has_previous = false;

  buffer::Frame frame;
  while (buffer.Pop(frame))
  {
    EXPECT_GT(frame.metadata.duration, 0.0);
    AssertWithinTolerance(frame.metadata.duration,
                          1.0 / config.target_fps,
                          1e-6,
                          "Duration must align with target FPS");

    if (has_previous)
    {
      EXPECT_GE(frame.metadata.pts, previous.metadata.pts);
    }
    previous = frame;
    has_previous = true;
  }
}

// Rule: MT-005 Prometheus Metrics (MetricsAndTimingContract.md §MT-005)
TEST_F(MetricsAndTimingContractTest, MT_005_MetricsExporterReflectsChannelState)
{
  telemetry::MetricsExporter exporter(/*port=*/0);
  ChannelManagerStub manager;

  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://metrics/telemetry";
  config.target_fps = 24.0;

  auto runtime = manager.StartChannel(101, config, exporter, /*buffer_capacity=*/8);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  telemetry::ChannelMetrics metrics{};
  ASSERT_TRUE(exporter.GetChannelMetrics(101, metrics));
  EXPECT_NE(metrics.state, telemetry::ChannelState::STOPPED);
  EXPECT_GE(metrics.buffer_depth_frames, 1u);

  manager.StopChannel(runtime, exporter);
  ASSERT_TRUE(exporter.GetChannelMetrics(101, metrics));
  EXPECT_EQ(metrics.state, telemetry::ChannelState::STOPPED);
  EXPECT_EQ(metrics.buffer_depth_frames, 0u);
}

} // namespace

