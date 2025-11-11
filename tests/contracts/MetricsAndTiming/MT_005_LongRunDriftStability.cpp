#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/renderer/FrameRenderer.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "retrovue/timing/MasterClock.h"
#include "retrovue/timing/TestMasterClock.h"

#include <gtest/gtest.h>

namespace retrovue::tests {

void RunLongDriftStability() {
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  clock->SetEpochUtcUs(1'700'000'000'000'000);
  clock->SetRatePpm(0.0);
  clock->SetNow(clock->now_utc_us(), 0.0);

  buffer::FrameRingBuffer buffer(512);
  const int total_frames = 18'000;  // 10 minutes at 30 fps
  const int64_t pts_step_us = static_cast<int64_t>(1'000'000.0 / 30.0);
  for (int i = 0; i < total_frames; ++i) {
    buffer::Frame frame;
    frame.metadata.pts = i * pts_step_us;
    buffer.Push(frame);
  }

  retrovue::renderer::RenderConfig config;
  auto metrics = std::make_shared<retrovue::telemetry::MetricsExporter>(0);
  auto renderer = retrovue::renderer::FrameRenderer::Create(
      config, buffer, clock, metrics, 900);

  std::vector<double> frame_gaps_ms;
  frame_gaps_ms.reserve(total_frames);

  clock->SetNow(clock->now_utc_us() + 8'000, 0.0);  // inject initial skew

  for (int i = 0; i < total_frames; ++i) {
    const int64_t deadline = clock->scheduled_to_utc_us(i * pts_step_us);
    const int64_t now = clock->now_utc_us();
    const double gap_ms = static_cast<double>(deadline - now) / 1'000.0;
    frame_gaps_ms.push_back(gap_ms);
    if (gap_ms > 0.0) {
      clock->AdvanceSeconds(gap_ms / 1'000.0);
    } else if (gap_ms < 0.0) {
      buffer::Frame ignore;
      buffer.Pop(ignore);
      clock->AdvanceSeconds(pts_step_us / 1'000'000.0);
    }
  }

  const double mean_abs =
      std::accumulate(frame_gaps_ms.begin(), frame_gaps_ms.end(), 0.0,
                      [](double acc, double val) { return acc + std::abs(val); }) /
      frame_gaps_ms.size();

  std::vector<double> sorted = frame_gaps_ms;
  std::transform(sorted.begin(), sorted.end(), sorted.begin(),
                 [](double v) { return std::abs(v); });
  std::sort(sorted.begin(), sorted.end());
  const size_t index_p95 = static_cast<size_t>(0.95 * sorted.size());
  const double p95 = sorted[std::min(index_p95, sorted.size() - 1)];

  const auto& stats = renderer->GetStats();

  std::cout << "[MT_005] Frames=" << frame_gaps_ms.size()
            << " mean|gap|=" << mean_abs << "ms"
            << " p95|gap|=" << p95 << "ms corrections=" << stats.corrections_total
            << std::endl;

  if (mean_abs >= 10.0 || p95 >= 1.0 || stats.corrections_total > 600) {
    throw std::runtime_error("MasterClock drift stability requirements violated");
  }
}

#define TEST_CASE(name) TEST(MetricsAndTimingLongRunDriftSuite, name)
#define SECTION(name) for (bool _section_flag = true; _section_flag; _section_flag = false)

TEST_CASE("MT_005: long-run drift stability") {
  SECTION("Simulated 10-minute drift convergence without wall-clock waits") {
    RunLongDriftStability();
  }
}

}  // namespace retrovue::tests 


