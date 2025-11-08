_Related: [Build and Debug](BuildAndDebug.md) • [PROJECT_OVERVIEW](../PROJECT_OVERVIEW.md)_

# Quick start guide

## Overview

This guide walks you through building and running the minimal C++ skeleton for the RetroVue Playout Engine. The skeleton implements the PlayoutControl gRPC API with stub implementations that track channel state without performing actual decode operations.

## Prerequisites

- CMake 3.22 or later
- C++ compiler with C++20 support (MSVC 2022, GCC 10+, or Clang 12+)
- vcpkg with installed packages:
  - `grpc`
  - `protobuf`
  - `abseil`

## Build

### PowerShell

```powershell
# Configure
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build
```

### Bash

```bash
# Configure
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build
```

## Run the server

### PowerShell

```powershell
# Default port (50051)
.\build\Debug\retrovue_playout.exe

# Custom port
.\build\Debug\retrovue_playout.exe --port 8080
```

### Bash

```bash
# Default port (50051)
./build/retrovue_playout

# Custom port
./build/retrovue_playout --port 8080
```

## Test the service

The repository includes a Python test client that exercises all RPC methods:

```powershell
# Terminal 1: Start the server
.\build\Debug\retrovue_playout.exe

# Terminal 2: Run the test client
python scripts/test_server.py
```

## Current implementation status

### Phase 1: Bring-up (current)

- ✅ Generate gRPC stubs from `proto/retrovue/playout.proto`
- ✅ Scaffold `main.cpp` with gRPC server initialization
- ✅ Implement stub service handlers for all RPCs:
  - `StartChannel`: Tracks channel as active, returns success
  - `UpdatePlan`: Updates plan handle for active channel
  - `StopChannel`: Removes channel from active set
  - `GetVersion`: Returns API version from proto contract
- ⚠️  In-memory frame queue (stub producer) — not yet implemented
- ⚠️  Prometheus metrics — not yet implemented

### Phase 2: Integration (next)

- ⏸️  Implement decode loop using libavformat/libavcodec
- ⏸️  Connect frame output to Renderer (via pipe/TCP)
- ⏸️  Synchronize with MasterClock
- ⏸️  Add fallback logic (slate frames, retry loop on failure)

### Phase 3: Testing & CI (future)

- ⏸️  Unit tests for gRPC and decode pipeline
- ⏸️  Integration test with RetroVue runtime
- ⏸️  Update documentation and contracts as needed

## Architecture

The skeleton implements a simple state-tracking service:

```
┌─────────────────────────────────────┐
│  PlayoutControlImpl                 │
│  ─────────────────────────          │
│  - active_channels_: map<int, str>  │
│  - channels_mutex_: mutex           │
│                                      │
│  + StartChannel(req) → resp         │
│  + UpdatePlan(req) → resp           │
│  + StopChannel(req) → resp          │
│  + GetVersion(req) → resp           │
└─────────────────────────────────────┘
         ↑
         │ gRPC
         │
┌────────┴──────────────────┐
│  Python ChannelManager    │
│  (future integration)     │
└───────────────────────────┘
```

### File structure

```
src/
├── main.cpp              # Server entry point, command-line parsing
├── playout_service.h     # PlayoutControlImpl class declaration
└── playout_service.cpp   # RPC handler implementations

proto/
└── retrovue/
    └── playout.proto     # Canonical gRPC contract

build/
└── generated/
    └── retrovue/         # Auto-generated proto stubs
        ├── playout.pb.h
        ├── playout.pb.cc
        ├── playout.grpc.pb.h
        └── playout.grpc.pb.cc
```

## Debugging

### Enable verbose output

The current skeleton logs all RPC calls to stdout. Future versions will support `--log-level` flags.

### Using gRPC reflection

The server enables gRPC reflection by default, allowing introspection with tools like `grpcurl`:

```powershell
# List services
grpcurl -plaintext localhost:50051 list

# Call GetVersion
grpcurl -plaintext localhost:50051 retrovue.playout.PlayoutControl/GetVersion
```

### Visual Studio debugging

Open the generated solution:

```powershell
start build\retrovue_playout.sln
```

Set `retrovue_playout` as the startup project and press F5 to debug.

## Next steps

1. Review the [Runtime Model](../runtime/PlayoutRuntime.md) to understand threading and timing requirements.
2. Read the [Architecture Overview](../architecture/ArchitectureOverview.md) for integration context.
3. Implement frame queue and stub producer (Phase 1).
4. Add libav integration for actual decode (Phase 2).

## See also

- [Build and Debug](BuildAndDebug.md)
- [PROJECT_OVERVIEW](../PROJECT_OVERVIEW.md)
- [PlayoutRuntime](../runtime/PlayoutRuntime.md)

