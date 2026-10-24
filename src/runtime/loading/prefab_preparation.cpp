// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_preparation.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"

namespace gneiss::runtime {
struct prefab_preparation::state {
  gneiss::engine::prepared_scene_description prepared;
  gneiss::engine::scene_diagnostic diagnostic;
  gneiss::engine::result result{gneiss::engine::result::invalid_state};
};
prefab_preparation::prefab_preparation(gneiss::engine::task_executor& executor)
    : executor_(executor), scope_(executor.make_scope()), state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() {
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
}
gneiss::engine::result
prefab_preparation::create(runtime::application& application,
                           gneiss::engine::task_executor& executor, std::string_view uri,
                           std::unique_ptr<prefab_preparation>& output) noexcept try {
  output.reset();
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::result::success
                              : gneiss::engine::result::invalid_state;
  if (validation != gneiss::engine::result::success) {
    return validation;
  }
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation(executor));
  if (candidate->scope_.id == 0U) {
    return gneiss::engine::result::not_ready;
  }
  gneiss::engine::task_handle task;
  const auto submitted = executor.submit(
      {.name = "prefab.prepare", .scope = candidate->scope_},
      [result = candidate->state_, files = host->asset_files(),
       uri = std::string(uri)](const gneiss::engine::task_context& context) {
        result->result = gneiss::engine::prepare_prefab_description(
            files, uri, result->prepared, result->diagnostic,
            [&] { return context.stop_requested(); });
        auto state = result->result == gneiss::engine::result::success
                         ? gneiss::engine::task_state::succeeded
                         : gneiss::engine::task_state::failed;
        if (context.stop_requested()) {
          state = gneiss::engine::task_state::cancelled;
        }
        return gneiss::engine::task_outcome{.state = state};
      },
      task);
  if (submitted != gneiss::engine::submit_result::success) {
    return gneiss::engine::result::not_ready;
  }
  output = std::move(candidate);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
void prefab_preparation::cancel() {
  cancelled_ = true;
  executor_.cancel_scope(scope_);
}
gneiss::engine::result prefab_preparation::poll(gneiss::engine::prepared_scene_description& output,
                                                bool& complete) {
  complete = false;
  std::vector<gneiss::engine::task_completion> completions;
  if (executor_.poll(scope_, completions, 1U) == 0U) {
    return gneiss::engine::result::success;
  }
  complete = true;
  if (cancelled_ || completions.front().outcome.state == gneiss::engine::task_state::cancelled) {
    return gneiss::engine::result::invalid_state;
  }
  if (state_->result == gneiss::engine::result::success) {
    output = std::move(state_->prepared);
  }
  return state_->result;
}
}
