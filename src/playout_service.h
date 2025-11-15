// Repository: Retrovue-playout
// Component: PlayoutControl gRPC Service Implementation
// Purpose: Implements the PlayoutControl service interface for channel lifecycle management.
// Copyright (c) 2025 RetroVue

#ifndef RETROVUE_PLAYOUT_SERVICE_H_
#define RETROVUE_PLAYOUT_SERVICE_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "playout.grpc.pb.h"
#include "playout.pb.h"
#include "retrovue/runtime/PlayoutController.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "retrovue/timing/MasterClock.h"

namespace retrovue {
namespace playout {

// PlayoutControlImpl implements the gRPC service defined in playout.proto.
// This is a thin adapter that delegates to PlayoutController.
class PlayoutControlImpl final : public PlayoutControl::Service {
 public:
  // Constructs the service with a controller that manages channel lifecycle.
  PlayoutControlImpl(std::shared_ptr<runtime::PlayoutController> controller);
  ~PlayoutControlImpl() override;

  // Disable copy and move
  PlayoutControlImpl(const PlayoutControlImpl&) = delete;
  PlayoutControlImpl& operator=(const PlayoutControlImpl&) = delete;

  // RPC implementations
  grpc::Status StartChannel(grpc::ServerContext* context,
                            const StartChannelRequest* request,
                            StartChannelResponse* response) override;

  grpc::Status UpdatePlan(grpc::ServerContext* context,
                          const UpdatePlanRequest* request,
                          UpdatePlanResponse* response) override;

  grpc::Status StopChannel(grpc::ServerContext* context,
                           const StopChannelRequest* request,
                           StopChannelResponse* response) override;

  grpc::Status GetVersion(grpc::ServerContext* context,
                          const ApiVersionRequest* request,
                          ApiVersion* response) override;

  grpc::Status LoadPreview(grpc::ServerContext* context,
                           const LoadPreviewRequest* request,
                           LoadPreviewResponse* response) override;

  grpc::Status SwitchToLive(grpc::ServerContext* context,
                            const SwitchToLiveRequest* request,
                            SwitchToLiveResponse* response) override;

 private:
  // Controller that manages all channel lifecycle operations
  std::shared_ptr<runtime::PlayoutController> controller_;
};

}  // namespace playout
}  // namespace retrovue

#endif  // RETROVUE_PLAYOUT_SERVICE_H_

