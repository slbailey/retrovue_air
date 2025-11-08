# RetroVue Playout Engine – Developer Overview

_Related: [Architecture overview](architecture/ArchitectureOverview.md) • [Runtime model](runtime/PlayoutRuntime.md) • [Proto schema](../proto/retrovue/playout.proto)_

---

## Purpose

The **RetroVue Playout Engine** implements a native C++ backend that transforms playout plans (from the ChannelManager) into real-time decoded frame streams. It serves as the bridge between high-level Python orchestration and the low-level frame delivery required by the Renderer.

- **Control interface:** Communicates with the RetroVue Python runtime using gRPC (`proto/retrovue/playout.proto`).
- **Output:** Streams decoded frames to the Renderer, which emits MPEG-TS per virtual channel.
- **Timing:** Synchronizes all output using the system-wide `MasterClock` for deterministic alignment.

---

## Component Overview

| Component             | Language        | Responsibility                                |
| --------------------- | --------------- | --------------------------------------------- |
| **RetroVue Core**     | Python          | Scheduling, orchestration, channel management |
| **RetroVue Renderer** | Python + ffmpeg | MPEG-TS encoding & transport                  |
| **Playout Engine**    | C++             | Decode, buffer, and align to MasterClock      |

Each component communicates over a documented API surface. The C++ playout engine **does not** implement scheduling logic — this remains in the RetroVue Core.

---

## Contracts & Interfaces

| Type    | Path/Location                  | Description                                      |
| ------- | ------------------------------ | ------------------------------------------------ |
| gRPC    | `proto/retrovue/playout.proto` | Control API: ChannelManager ↔ Playout (required) |
| Metrics | Prometheus `/metrics` endpoint | Channel state, frame gap telemetry               |
| Build   | `CMakeLists.txt`               | Defines `retrovue_playout` and dependencies      |

**Workflow:**  
_New features start by updating the contract_ (proto, metrics, etc.) in `docs/contracts/` and `proto/`, **before** implementing new functionality.

---

## Building & Running

**Build (Release)**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Run**

```bash
./build/retrovue_playout --channel 1 --port 8090
```

---

## Communication Model

- **Control API (gRPC):** Receives channel start/stop/update commands from the ChannelManager (Python client).
- **Frame Bus:** Shares decoded frames with the Renderer via a per-channel ring buffer.
- **Telemetry:** Publishes health and state metrics at the Prometheus `/metrics` endpoint.

---

## Development Status

### Phase 1: gRPC Skeleton ✅ Complete

- ✅ gRPC service definition and implementation
- ✅ `StartChannel`, `UpdatePlan`, `StopChannel` RPCs
- ✅ CMake build system with vcpkg integration
- ✅ Python test client

### Phase 2: Frame Buffer & Stub Decode ✅ Complete

- ✅ Lock-free circular frame buffer (FrameRingBuffer)
- ✅ Frame producer with stub decode (synthetic frames)
- ✅ Dedicated decode thread per channel
- ✅ Prometheus metrics schema
- ✅ Unit tests and integration tests

### Phase 3: Real Decode + Renderer + Metrics ✅ Complete

- ✅ FFmpegDecoder with libavformat/libavcodec
- ✅ Multi-codec support (H.264, HEVC, VP9, AV1)
- ✅ FrameRenderer (headless + preview modes)
- ✅ MetricsHTTPServer with native HTTP/1.1 implementation
- ✅ Complete decode → buffer → render → metrics pipeline
- ✅ Production-grade performance (<10ms decode latency @ 1080p30)

### Phase 4: Production Hardening 📋 Planned

- [ ] MasterClock integration for frame-accurate timing
- [ ] Multi-channel stress testing (10+ simultaneous channels)
- [ ] Error recovery and slate frame fallback
- [ ] Hardware decode acceleration (NVDEC, QSV, VideoToolbox)
- [ ] Operational tooling (Grafana dashboards, Prometheus alerts)

**See:** [Roadmap](milestones/Roadmap.md) for detailed plans

---

## Notes & House Rules

- This repository is **not** the owner of scheduling logic. Scheduling is _authoritative_ in the RetroVue Core (Python).
- Always treat Python → C++ interactions as client → server.
- Keep all timing and output strictly deterministic based on the MasterClock.
- All API and integration changes **must** follow the `docs/contracts/` workflow (contract first, then implementation).

---

_For further details, see:_

- [docs/README.md](README.md)
- [runtime/PlayoutRuntime.md](runtime/PlayoutRuntime.md)
- [developer/BuildAndDebug.md](developer/BuildAndDebug.md)
