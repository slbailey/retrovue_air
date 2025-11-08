_Related: [Architecture overview](../architecture/ArchitectureOverview.md) • [Playout runtime](../runtime/PlayoutRuntime.md)_

# Deployment integration

## Purpose

Document infrastructure requirements, supported deployment models, and operational tooling for the playout engine.

## Supported environments

- **Local development:** Windows or Linux host, Python runtime, and playout engine running side by side.
- **Staging / production:** Linux hosts with systemd supervision, Renderer running on the same or adjacent host, Prometheus/Grafana monitoring stack.

## Dependencies

- **Runtime libraries:** libavcodec, libavformat, libswscale, and their transitive FFmpeg dependencies.
- **gRPC toolchain:** `protoc` compiler with C++ and Python plugins for generating bindings from `proto/retrovue/playout.proto`.
- **Telemetry:** Prometheus scraper configured against the playout engine’s `/metrics` endpoint (default port `9308`).

## Process orchestration

- Service name recommendation: `retrovue-playout`.
- Suggested systemd unit snippet:

```ini
[Unit]
Description=RetroVue Playout Engine
After=network.target

[Service]
ExecStart=/opt/retrovue-playout/bin/retrovue_playout --config /etc/retrovue-playout/config.toml
Restart=on-failure
RestartSec=5
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

- Configuration file should declare channel bindings, Renderer endpoints, logging level, and metrics port.

## Networking

- gRPC control plane can operate in-process or via Unix domain socket at `/var/run/retrovue/playout.sock`.
- Renderer consumes frames through shared memory pipes (`\\.\pipe\retrovue-playout-<channel-id>` on Windows or `/run/retrovue/playout-<channel-id>` on Linux) or TCP sockets where shared memory is unavailable.
- MPEG-TS output defaults to `http://localhost:<port>/channel/<channel-id>.ts`; ensure reverse proxies pass through chunked transfer encoding.

## Observability

- Collect Prometheus metrics for channel state, frame gaps, restart counts, and build metadata.
- Forward structured logs to the RetroVue central logging stack (Elastic, Loki, or equivalent).
- Optional: expose a health check endpoint that returns 200 when all channels are `ready`.

## Security considerations

- Restrict Unix domain socket permissions to the RetroVue runtime user.
- Validate configuration files using RetroVue’s shared config schema tooling before deployment.
- Keep FFmpeg dependencies patched to address codec-level CVEs.

## Disaster recovery

- Keep nightly backups of configuration and playout plans to allow rapid reprovisioning.
- Store build artifacts in RetroVue’s artifact registry with semantic version tags matching `PLAYOUT_API_VERSION`.
- Document manual failover steps for redirecting channels to a warm standby playout engine.

## See also

- [Playout runtime](../runtime/PlayoutRuntime.md)
- [Developer guide](../developer/BuildAndDebug.md)
- [RetroVue deployment bootstrap](../../Retrovue/docs/infra/bootstrap.md)

