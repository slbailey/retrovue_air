// Repository: Retrovue-playout
// Component: PlayoutControl gRPC Service Implementation
// Purpose: Implements the PlayoutControl service interface for channel lifecycle management.
// Copyright (c) 2025 RetroVue

#include "playout_service.h"

#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace retrovue
{
  namespace playout
  {

    namespace
    {
      constexpr char kApiVersion[] = "1.0.0";
    } // namespace

    PlayoutControlImpl::PlayoutControlImpl(
        std::shared_ptr<runtime::PlayoutController> controller)
        : controller_(std::move(controller))
    {
      std::cout << "[PlayoutControlImpl] Service initialized (API version: " << kApiVersion << ")" << std::endl;
    }

    PlayoutControlImpl::~PlayoutControlImpl()
    {
      std::cout << "[PlayoutControlImpl] Service shutting down" << std::endl;
      // Controller handles cleanup
    }

    grpc::Status PlayoutControlImpl::StartChannel(grpc::ServerContext *context,
                                                  const StartChannelRequest *request,
                                                  StartChannelResponse *response)
    {
      const int32_t channel_id = request->channel_id();
      const std::string &plan_handle = request->plan_handle();
      const int32_t port = request->port();
      // UDS path is optional - check if field exists in proto
      std::optional<std::string> uds_path = std::nullopt;

      std::cout << "[StartChannel] Request received: channel_id=" << channel_id
                << ", plan_handle=" << plan_handle << ", port=" << port << std::endl;

      // Delegate to controller
      auto result = controller_->StartChannel(channel_id, plan_handle, port, uds_path);
      
      response->set_success(result.success);
      response->set_message(result.message);
      
      if (!result.success) {
        grpc::StatusCode code = grpc::StatusCode::INTERNAL;
        if (result.message.find("already") != std::string::npos) {
          code = grpc::StatusCode::ALREADY_EXISTS;
        } else if (result.message.find("not found") != std::string::npos) {
          code = grpc::StatusCode::NOT_FOUND;
        }
        return grpc::Status(code, result.message);
      }
      
      std::cout << "[StartChannel] Channel " << channel_id << " started successfully" << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::UpdatePlan(grpc::ServerContext *context,
                                                const UpdatePlanRequest *request,
                                                UpdatePlanResponse *response)
    {
      const int32_t channel_id = request->channel_id();
      const std::string &plan_handle = request->plan_handle();

      std::cout << "[UpdatePlan] Request received: channel_id=" << channel_id
                << ", plan_handle=" << plan_handle << std::endl;

      // Delegate to controller
      auto result = controller_->UpdatePlan(channel_id, plan_handle);
      
      response->set_success(result.success);
      response->set_message(result.message);
      
      if (!result.success) {
        grpc::StatusCode code = grpc::StatusCode::INTERNAL;
        if (result.message.find("not found") != std::string::npos) {
          code = grpc::StatusCode::NOT_FOUND;
        }
        return grpc::Status(code, result.message);
      }
      
      std::cout << "[UpdatePlan] Channel " << channel_id << " plan updated successfully" << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::StopChannel(grpc::ServerContext *context,
                                                 const StopChannelRequest *request,
                                                 StopChannelResponse *response)
    {
      const int32_t channel_id = request->channel_id();
      std::cout << "[StopChannel] Request received: channel_id=" << channel_id << std::endl;
      
      // Delegate to controller
      auto result = controller_->StopChannel(channel_id);
      
      response->set_success(result.success);
      response->set_message(result.message);
      
      if (!result.success) {
        grpc::StatusCode code = grpc::StatusCode::INTERNAL;
        if (result.message.find("not found") != std::string::npos) {
          code = grpc::StatusCode::NOT_FOUND;
        }
        return grpc::Status(code, result.message);
      }
      
      std::cout << "[StopChannel] Channel " << channel_id << " stopped successfully" << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::GetVersion(grpc::ServerContext *context,
                                                const ApiVersionRequest *request,
                                                ApiVersion *response)
    {
      std::cout << "[GetVersion] Request received" << std::endl;
      response->set_version(kApiVersion);
      std::cout << "[GetVersion] Returning version: " << kApiVersion << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::LoadPreview(grpc::ServerContext *context,
                                                 const LoadPreviewRequest *request,
                                                 LoadPreviewResponse *response)
    {
      const int32_t channel_id = request->channel_id();
      const std::string &asset_path = request->asset_path();

      std::cout << "[LoadPreview] Request received: channel_id=" << channel_id
                << ", asset_path=" << asset_path << std::endl;

      // Delegate to controller
      auto result = controller_->LoadPreview(channel_id, asset_path);
      
      response->set_success(result.success);
      response->set_message(result.message);
      response->set_shadow_decode_started(result.shadow_decode_started);
      
      if (!result.success) {
        grpc::StatusCode code = grpc::StatusCode::INTERNAL;
        if (result.message.find("not found") != std::string::npos) {
          code = grpc::StatusCode::NOT_FOUND;
        } else if (result.message.find("not initialized") != std::string::npos ||
                   result.message.find("not available") != std::string::npos) {
          code = grpc::StatusCode::FAILED_PRECONDITION;
        }
        return grpc::Status(code, result.message);
      }
      
      std::cout << "[LoadPreview] Channel " << channel_id
                << " preview load " << (result.shadow_decode_started ? "succeeded" : "failed")
                << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::SwitchToLive(grpc::ServerContext *context,
                                                  const SwitchToLiveRequest *request,
                                                  SwitchToLiveResponse *response)
    {
      const int32_t channel_id = request->channel_id();

      std::cout << "[SwitchToLive] Request received: channel_id=" << channel_id << std::endl;

      // Delegate to controller
      auto result = controller_->SwitchToLive(channel_id);
      
      response->set_success(result.success);
      response->set_message(result.message);
      response->set_pts_contiguous(result.pts_contiguous);
      response->set_live_start_pts(result.live_start_pts);
      
      if (!result.success) {
        grpc::StatusCode code = grpc::StatusCode::INTERNAL;
        if (result.message.find("not found") != std::string::npos) {
          code = grpc::StatusCode::NOT_FOUND;
        } else if (result.message.find("not initialized") != std::string::npos ||
                   result.message.find("not running") != std::string::npos) {
          code = grpc::StatusCode::FAILED_PRECONDITION;
        }
        return grpc::Status(code, result.message);
      }
      
      std::cout << "[SwitchToLive] Channel " << channel_id
                << " switch " << (result.success ? "succeeded" : "failed")
                << ", PTS contiguous: " << std::boolalpha << result.pts_contiguous << std::endl;
      return grpc::Status::OK;
    }


  } // namespace playout
} // namespace retrovue
