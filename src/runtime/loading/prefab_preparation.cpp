// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_preparation.hpp"
#include "engine/core/error.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "runtime/application.hpp"

namespace gneiss::runtime {
struct prefab_preparation::state {
  engine::asset_request<engine::scene_instantiation_data> request;
};
prefab_preparation::prefab_preparation() : state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() = default;

gneiss::engine::error
prefab_preparation::create(runtime::application& application,
                           gneiss::engine::task_executor& executor, std::string_view uri,
                           std::unique_ptr<prefab_preparation>& output) noexcept try {
  output.reset();
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::error::success
                              : gneiss::engine::error::invalid_state;
  if (validation != gneiss::engine::error::success) {
    return validation;
  }
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation());
  auto& assets = host->assets();
  if (const auto configured = assets.configure(executor); configured.failed())
    return configured;
  auto prepared = assets.prepare_prefab(uri, true);
  if (!prepared)
    return prepared.error();
  candidate->state_->request = std::move(*prepared);
  output = std::move(candidate);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
void prefab_preparation::cancel() {
  cancelled_ = true;
  state_->request.cancel();
}
gneiss::engine::error prefab_preparation::poll(gneiss::engine::scene_instantiation_data& output,
                                               bool& complete) {
  complete = false;
  if (cancelled_) {
    complete = true;
    return engine::error::invalid_state;
  }
  auto prepared = state_->request.take();
  if (!prepared) {
    if (prepared.error().error == engine::error::not_ready)
      return engine::error::success;
    complete = true;
    return prepared.error().error;
  }
  output = **prepared;
  complete = true;
  return engine::error::success;
}
}
