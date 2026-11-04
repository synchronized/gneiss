// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_preparation.hpp"
#include "engine/core/error.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "runtime/application.hpp"

namespace gneiss::runtime {
struct prefab_preparation::state {
  std::unique_ptr<gneiss::engine::scene_description_request> request;
  gneiss::engine::scene_preparation_inputs inputs;
  std::string uri;
  bool submitted{};
  gneiss::engine::scene_instantiation_data prepared;
  gneiss::engine::scene_diagnostic diagnostic;
  gneiss::engine::error result{gneiss::engine::error::invalid_state};
};
prefab_preparation::prefab_preparation(gneiss::engine::task_executor& executor)
    : executor_(executor), scope_(executor.make_scope()), state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() {
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
}
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
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation(executor));
  if (candidate->scope_.id == 0U) {
    return gneiss::engine::error::not_ready;
  }
  auto& assets = host->assets();
  assets.attach_task_executor(executor);
  candidate->state_->uri = uri;
  candidate->state_->request =
      std::make_unique<gneiss::engine::scene_description_request>(assets, std::string(uri), true);
  output = std::move(candidate);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
void prefab_preparation::cancel() {
  cancelled_ = true;
  executor_.cancel_scope(scope_);
}
gneiss::engine::error prefab_preparation::poll(gneiss::engine::scene_instantiation_data& output,
                                               bool& complete) {
  complete = false;
  if (!state_->submitted) {
    if (cancelled_) {
      state_->request.reset();
      complete = true;
      return gneiss::engine::error::invalid_state;
    }
    if (state_->request) {
      const auto status = state_->request->advance(state_->inputs, state_->diagnostic);
      if (status == gneiss::engine::error::not_ready)
        return gneiss::engine::error::success;
      if (status.failed()) {
        complete = true;
        return status;
      }
      state_->request.reset();
    }
    gneiss::engine::task_handle task;
    const auto submitted = executor_.submit(
        {.name = "prefab.prepare", .scope = scope_},
        [result = state_](const gneiss::engine::task_context& context) {
          result->result = gneiss::engine::prepare_loaded_scene(
              result->inputs, result->uri, result->prepared, result->diagnostic,
              [&] { return context.stop_requested(); });
          return gneiss::engine::task_outcome{.state = result->result.ok()
                                                           ? gneiss::engine::task_state::succeeded
                                                           : gneiss::engine::task_state::failed};
        },
        task);
    if (submitted == gneiss::engine::submit_result::full)
      return gneiss::engine::error::success;
    if (submitted != gneiss::engine::submit_result::success) {
      complete = true;
      return gneiss::engine::error::invalid_state;
    }
    state_->submitted = true;
    return gneiss::engine::error::success;
  }
  std::vector<gneiss::engine::task_completion> completions;
  if (executor_.poll(scope_, completions, 1U) == 0U) {
    return gneiss::engine::error::success;
  }
  complete = true;
  if (cancelled_ || completions.front().outcome.state == gneiss::engine::task_state::cancelled) {
    return gneiss::engine::error::invalid_state;
  }
  if (state_->result == gneiss::engine::error::success) {
    output = std::move(state_->prepared);
  }
  return state_->result;
}
}
