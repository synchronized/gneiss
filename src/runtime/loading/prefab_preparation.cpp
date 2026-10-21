// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_preparation.hpp"
#include "runtime/application.hpp"

namespace gneiss::runtime {
struct prefab_preparation::state {
  engine::scene::prepared_scene_description prepared;
  engine::resource::scene_diagnostic diagnostic;
  gneiss_result result{GNEISS_ERROR_INVALID_STATE};
};
prefab_preparation::prefab_preparation(engine::core::tasks::task_executor& executor)
    : executor_(executor), scope_(executor.make_scope()), state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() {
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
}
gneiss_result prefab_preparation::create(runtime::application& application,
                                         engine::core::tasks::task_executor& executor,
                                         std::string_view uri,
                                         std::unique_ptr<prefab_preparation>& output) noexcept try {
  output.reset();
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? GNEISS_SUCCESS
                              : GNEISS_ERROR_INVALID_STATE;
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation(executor));
  if (candidate->scope_.id == 0U) {
    return GNEISS_ERROR_NOT_READY;
  }
  engine::core::tasks::task_handle task;
  const auto submitted = executor.submit(
      {.name = "prefab.prepare", .scope = candidate->scope_},
      [result = candidate->state_, files = host->asset_files(),
       uri = std::string(uri)](const engine::core::tasks::task_context& context) {
        result->result = engine::scene::prepare_prefab_description(
            files, uri, result->prepared, result->diagnostic,
            [&] { return context.stop_requested(); });
        auto state = result->result == GNEISS_SUCCESS ? engine::core::tasks::task_state::succeeded
                                                      : engine::core::tasks::task_state::failed;
        if (context.stop_requested()) {
          state = engine::core::tasks::task_state::cancelled;
        }
        return engine::core::tasks::task_outcome{.state = state};
      },
      task);
  if (submitted != engine::core::tasks::submit_result::success) {
    return GNEISS_ERROR_NOT_READY;
  }
  output = std::move(candidate);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
void prefab_preparation::cancel() {
  cancelled_ = true;
  executor_.cancel_scope(scope_);
}
gneiss_result prefab_preparation::poll(engine::scene::prepared_scene_description& output,
                                       bool& complete) {
  complete = false;
  std::vector<engine::core::tasks::task_completion> completions;
  if (executor_.poll(scope_, completions, 1U) == 0U) {
    return GNEISS_SUCCESS;
  }
  complete = true;
  if (cancelled_ ||
      completions.front().outcome.state == engine::core::tasks::task_state::cancelled) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (state_->result == GNEISS_SUCCESS) {
    output = std::move(state_->prepared);
  }
  return state_->result;
}
}
