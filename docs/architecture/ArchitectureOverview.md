_Related: [Runtime model](../runtime/PlayoutRuntime.md) • [RetroVue architecture overview](../../Retrovue/docs/architecture/ArchitectureOverview.md)_

# Architecture overview

## Purpose

Describe how the native C++ playout engine fits into the RetroVue architecture and how it collaborates with the Python runtime, Renderer, and surrounding infrastructure.

## System context

- The ChannelManager generates playout plans and invokes the playout engine through a gRPC control surface.
- The playout engine decodes media assets using libavformat/libavcodec and yields frames over a shared ring buffer.
- The Renderer consumes frames and emits MPEG-TS streams served to clients at `http://localhost:<port>/channel/<channel-id>.ts`.
- Prometheus scrapes health metrics exposed by the playout engine to confirm channel readiness and timing accuracy.

## Core subsystems

- **Control plane:** gRPC service defined in `proto/retrovue/playout.proto` that orchestrates channel lifecycle events.
- **Decode pipeline:** Demux and decode threads tuned per codec profile to maintain a lead time ahead of the Renderer.
- **Frame staging:** Lock-free ring buffers that guarantee minimum and maximum buffer depths for each channel.
- **Telemetry:** Metrics and structured logs emitted for monitoring, debugging, and operator visibility.

## Data and timing flow

1. ChannelManager publishes a playout plan via `StartChannel` or `UpdatePlan`.
2. The playout engine resolves asset URIs, demuxes packets, and decodes frames while tracking PTS/DTS.
3. Frames are staged with timing metadata and asset provenance before the Renderer consumes them.
4. The Renderer packages frames into MPEG-TS, keeping output aligned with the MasterClock maintained by the runtime.

## Deployment topology

- Local development runs the playout engine and Python runtime in the same host using in-process gRPC channels.
- Production deployments run the playout engine as a dedicated service communicating via Unix domain sockets.
- Multiple channel workers can run inside a single engine process; horizontal scaling is achieved by starting more engine instances.

## Failure domains

- **Decode failures:** Surface channel `error` state, trigger retries, and fall back to slate content.
- **Control plane disconnects:** ChannelManager retries gRPC connection with exponential backoff.
- **Renderer starvation:** Detected via buffer depth metrics and mitigated by slate injection until recovery.

## Evolution notes

- API versioning is governed by the `PLAYOUT_API_VERSION` constant. Any breaking change must bump the constant and coordinate releases between `retrovue-core` and `retrovue-playout`.
- Future extensions (e.g., adaptive bitrate ladders or remote hardware decoders) must maintain the same control plane contract or introduce versioned endpoints.

## See also

- [Runtime model](../runtime/PlayoutRuntime.md)
- [Deployment integration](../infra/Integration.md)
- [RetroVue core architecture](../../Retrovue/docs/architecture/ArchitectureOverview.md)

