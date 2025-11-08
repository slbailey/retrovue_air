# 🚀 Phase 3 – Real Decode + Renderer Integration

## Objective

Transition from synthetic frame generation to actual decoding using libavformat/libavcodec, and connect decoded frames to the renderer subsystem.  

## Goals

- Replace stubbed FrameProducer with real FFmpeg-based decoder
- Add `Renderer` module for frame visualization (OpenGL or CPU blit)
- Expose metrics via HTTP endpoint using Prometheus text format
- Integrate frame timing + PTS synchronization with playout clock
- Maintain thread-safe, lock-free design with minimal latency

## Key Deliverables

1. **Decode Layer**
   - Implement `FFmpegDecoder` in `src/decode/`
   - Support H.264 MP4 input via `avformat_open_input`
   - Push frames into `FrameRingBuffer`
   - Error handling + metrics export

2. **Renderer Layer**
   - Implement `FrameRenderer` interface
   - Support headless render (for testing) and preview window (debug)
   - Frame timing driven by metadata.pts

3. **Telemetry**
   - Add `MetricsHTTPServer` (src/telemetry/)
   - Expose Prometheus-compatible metrics on `localhost:9090/metrics`
   - Include per-channel buffer_depth, fps, frame_delay_ms

4. **Integration**
   - Extend `PlayoutService` to manage renderer lifecycle
   - Synchronize decode/render threads
   - Ensure clean stop/restart behavior

## Validation

- Unit tests for FFmpeg decode initialization and buffer output
- Integration tests verifying playback correctness
- Benchmark tests for latency and throughput

## Notes

- Phase 2 stub code will remain behind a `#define RETROVUE_STUB_DECODE` flag
- This phase introduces the first full media pipeline (decode → render → metrics)
- Target completion: End of current sprint

