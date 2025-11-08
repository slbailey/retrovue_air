This document describes the native C++ playout engine that underpins the RetroVue runtime’s real-time media delivery.
_Related: [RetroVue components – playout](../Retrovue/docs/components/playout.md) • [RetroVue runtime – renderer](../Retrovue/docs/runtime/Renderer.md)_

# RetroVue playout engine

The RetroVue playout engine turns ChannelManager schedules into decoded frame streams that the Renderer converts to MPEG-TS at `http://localhost:<port>/channel/<channel-num>.ts`, keeping every channel aligned with the MasterClock.

For detailed architecture and integration notes, see [docs/PROJECT_OVERVIEW.md](docs/PROJECT_OVERVIEW.md)

## Purpose

This component:

- Converts RetroVue playout plans into a continuous stream of decoded frames.
- Supplies the Renderer with frame buffers and timing metadata required for deterministic MPEG-TS output.
- Maintains clock alignment guarantees shared across the RetroVue runtime.
- Communicates with the RetroVue runtime via a gRPC control API defined in `proto/retrovue/playout.proto`.

## Architecture

The architecture centers on per-channel workers that mediate between the ChannelManager, storage, and the Renderer.

- Inputs come from channel configuration, ChannelManager playout plan segments, and asset metadata retrieved from RetroVue shared storage.
- Responsibilities include resolving asset URIs via libavformat/libavcodec, maintaining decode pipelines that stay ahead of the Renderer, and attaching precise PTS/DTS metadata to every frame.
- Out-of-scope concerns such as scheduling, plan generation, and MPEG-TS packaging remain owned by the RetroVue runtime and Renderer.

## Interfaces

The playout engine exposes three primary APIs that bind it into the wider RetroVue ecosystem.

- Control API (in-process gRPC):

  - `StartChannel(channel_id, plan_handle, port)` boots a dedicated decode loop.
  - `UpdatePlan(channel_id, plan_handle)` hot-swaps active plans when the scheduler issues deltas.
  - `StopChannel(channel_id)` drains buffers and releases decoder resources.

- Frame bus (shared ring buffer per channel):

  - Frames are pushed with PTS, DTS, duration, and asset provenance metadata.
  - The Renderer expects a minimum lead time of 150 ms and a soft ceiling of 500 ms.

- Health telemetry (Prometheus):
  - `retrovue_playout_channel_state{channel="N"}` reports `ready`, `buffering`, or `error`.
  - `retrovue_playout_frame_gap_seconds` measures deviation from ChannelManager timing.

```proto
service PlayoutControl {
  rpc StartChannel(StartChannelRequest) returns (StartChannelResponse);
  rpc UpdatePlan(UpdatePlanRequest) returns (UpdatePlanResponse);
  rpc StopChannel(StopChannelRequest) returns (StopChannelResponse);
}
```

### Control schema contract

- The canonical gRPC schema resides at `proto/retrovue/playout.proto`.
- Both the C++ engine and the Python runtime must build against the same `PLAYOUT_API_VERSION` embedded in the proto file options.
- Changes to the schema require regenerating C++ (`retrovue_playout_proto`) and Python stubs in lock-step so the ChannelManager and playout engine remain wire-compatible.

### Communication layer

The control surface between the Python ChannelManager and the playout engine is implemented using **gRPC**. All control traffic flows through a lightweight in-process or Unix-domain gRPC channel, depending on deployment configuration.

- **Transport:** In-process for local orchestration; Unix socket for multi-service deployments.
- **Schema:** Defined in `proto/retrovue/playout.proto` and compiled into both Python and C++ bindings.
- **Versioning:** Both sides must target the same `PLAYOUT_API_VERSION` constant. Backward-incompatible schema changes require a version bump and synchronized releases of `retrovue-core` and `retrovue-playout`.
- **Fallback path:** If gRPC initialization fails, the ChannelManager marks the channel `error` and retries the connection on an exponential backoff.

This gRPC interface is the sole contract between the RetroVue runtime and the native playout engine.

## Runtime model

The runtime model ensures deterministic decode and delivery across multiple channels.

- A single process hosts multiple channel workers; each worker contains:
  - A demux thread that reads packets through libavformat.
  - A decode thread pool sized per codec profile (defaults: H.264 = 2 threads, HEVC = 3 threads).
  - A frame staging queue capped at 90 frames (≈3 s at 30 fps).
- Workers synchronize against the MasterClock via monotonic timestamps.
- The Renderer pulls staged frames over shared memory or TCP (configurable) and emits MPEG-TS.
- The Renderer is intentionally decoupled from decode timing; it consumes whatever the playout engine publishes, ensuring the visual output stays locked to the MasterClock rather than the system wall clock.

## Failure and fallback behavior

Resilience paths keep channels running during transient issues.

- Asset open failures set the channel state to `error` and fall back to the slate loop (PNG → YUV).
- Buffer underruns insert the slate until the staging queue recovers above 30 frames.
- Decoder crashes trigger automatic restarts with exponential backoff (maximum five attempts per minute).
- The health exporter marks a channel `error` after five seconds without a successful decode.

## Operator workflows

Operators can build, run, and observe the engine using standard RetroVue tooling.

Build (Bash):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build (PowerShell):

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run a single channel (PowerShell example):

```powershell
.\build\retrovue_playout.exe --channel 1 --port 8090
```

Connect the Renderer for MPEG-TS output (PowerShell example):

```powershell
ffmpeg -re -f rawvideo -pix_fmt yuv420p -s 1920x1080 -r 30 `
  -i \\.\pipe\retrovue-playout-1 `
  -f mpegts http://localhost:8090/channel/1.ts
```

Observe telemetry:

```powershell
Invoke-WebRequest http://localhost:9308/metrics
```

Enable verbose diagnostics:

```powershell
.\build\retrovue_playout.exe --channel 1 --port 8090 --log-level trace
```

## Naming rules

Consistent naming keeps the playout engine aligned with RetroVue conventions.

- Executable binary: `retrovue_playout` (no camelCase variants).
- IPC endpoints: `retrovue-playout-<channel-id>` for pipes or sockets.
- Default Renderer HTTP base: `http://localhost:<base+channel-id>/channel/<channel-id>.ts`.
- Metrics labels use the numeric `channel` value issued by the ChannelManager.

## Documentation

### Developer Documentation

- [Development Standards](docs/development-standards.md): Defines folder structure, naming rules, and module layout conventions.

## Contributing

Follow RetroVue's contract-first workflow, keep documentation in sentence case, and open pull requests against this repository with cross-references back to RetroVue core changes when relevant. Every new feature or interface change must be accompanied by an update to this README and any corresponding RetroVue design documents.

## See also

- [RetroVue components – playout](../Retrovue/docs/components/playout.md)
- [RetroVue runtime – renderer](../Retrovue/docs/runtime/Renderer.md)
- [ChannelManager implementation](../Retrovue/src/retrovue/runtime/channel_manager.py)
