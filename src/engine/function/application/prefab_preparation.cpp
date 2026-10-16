// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/application/prefab_preparation.hpp"
#include "engine/function/application/application.hpp"
#include "engine/function/application/application_registry.hpp"

namespace gneiss::application_internal {
struct prefab_preparation::state {
  scene_internal::prepared_scene_description prepared;
  scene_internal::scene_diagnostic diagnostic;
  gneiss_result result{GNEISS_ERROR_INVALID_STATE};
};
prefab_preparation::prefab_preparation(tasks::task_executor& executor)
    : executor_(executor), scope_(executor.make_scope()), state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() {
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
}
gneiss_result prefab_preparation::create(gneiss_application application,
                                         tasks::task_executor& executor, std::string_view uri,
                                         std::unique_ptr<prefab_preparation>& output) noexcept try {
  output.reset();
  auto host = find_application(application);
  const auto validation = validate_application(host);
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation(executor));
  if (candidate->scope_.id == 0U) {
    return GNEISS_ERROR_NOT_READY;
  }
  tasks::task_handle task;
  const auto submitted = executor.submit(
      {.name = "prefab.prepare", .scope = candidate->scope_},
      [result = candidate->state_, files = host->asset_files(),
       uri = std::string(uri)](const tasks::task_context& context) {
        result->result = scene_internal::prepare_prefab_description(
            files, uri, result->prepared, result->diagnostic,
            [&] { return context.stop_requested(); });
        auto state = result->result == GNEISS_SUCCESS ? tasks::task_state::succeeded
                                                      : tasks::task_state::failed;
        if (context.stop_requested()) {
          state = tasks::task_state::cancelled;
        }
        return tasks::task_outcome{.state = state};
      },
      task);
  if (submitted != tasks::submit_result::success) {
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
gneiss_result prefab_preparation::poll(scene_internal::prepared_scene_description& output,
                                       bool& complete) {
  complete = false;
  std::vector<tasks::task_completion> completions;
  if (executor_.poll(scope_, completions, 1U) == 0U) {
    return GNEISS_SUCCESS;
  }
  complete = true;
  if (cancelled_ || completions.front().outcome.state == tasks::task_state::cancelled) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (state_->result == GNEISS_SUCCESS) {
    output = std::move(state_->prepared);
  }
  return state_->result;
}
}
