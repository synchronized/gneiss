// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/backend/caors/caors_action_map_request.hpp"
namespace gneiss::engine::resource {
namespace {
class caors_request final : public action_map_request {
public:
  explicit caors_request(caors::resources::load_request<engine::resource::action_map_asset> request)
      : request_(std::move(request)) {}
  ~caors_request() override { cancel(); }
  void cancel() noexcept override {
    (void)request_.cancel();
    request_ = {};
    terminal_ = result::invalid_state;
  }
  result take(engine::resource::action_map& output) noexcept override {
    if (terminal_ != result::not_ready)
      return terminal_;
    if (!request_.done())
      return result::not_ready;
    if (request_.cancelled()) {
      cancel();
      return terminal_;
    }
    auto status = result::success;
    if (!request_.loaded()) {
      const auto error = request_.error().value();
      status = error == caors::errc::not_found          ? result::not_found
               : error == caors::errc::invalid_argument ? result::invalid_argument
               : error == caors::errc::out_of_memory    ? result::out_of_memory
                                                        : result::io;
    } else {
      try {
        const auto asset = request_.try_get();
        if (!asset)
          status = result::internal;
        else {
          auto copy = asset->map;
          output = std::move(copy);
        }
      } catch (const std::bad_alloc&) {
        status = result::out_of_memory;
      } catch (...) {
        status = result::internal;
      }
    }
    request_ = {};
    terminal_ = status == result::success ? result::invalid_state : status;
    return status;
  }

private:
  caors::resources::load_request<engine::resource::action_map_asset> request_;
  result terminal_ = result::not_ready;
};
}
std::unique_ptr<action_map_request> make_action_map_request(
    caors::resources::load_request<engine::resource::action_map_asset> request) {
  return std::make_unique<caors_request>(std::move(request));
}
}
