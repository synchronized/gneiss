// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_preparation.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"

namespace gneiss::runtime {
struct prefab_preparation::state {
  engine::scene::prepared_scene_description prepared;
  engine::resource::scene_diagnostic diagnostic;
  gneiss::engine::core::result result{gneiss::engine::core::result::invalid_state};
};
prefab_preparation::prefab_preparation(engine::core::tasks::task_executor& executor)
    : executor_(executor), scope_(executor.make_scope()), state_(std::make_shared<state>()) {}
prefab_preparation::~prefab_preparation() {
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
}
gneiss::engine::core::result
prefab_preparation::create(runtime::application& application,
                           engine::core::tasks::task_executor& executor, std::string_view uri,
                           std::unique_ptr<prefab_preparation>& output) noexcept try {
  output.reset();
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::core::result::success
                              : gneiss::engine::core::result::invalid_state;
  if (validation != gneiss::engine::core::result::success) {
    return validation;
  }
  auto candidate = std::unique_ptr<prefab_preparation>(new prefab_preparation(executor));
  if (candidate->scope_.id == 0U) {
    return gneiss::engine::core::result::not_ready;
  }
  engine::core::tasks::task_handle task;
  const auto submitted = executor.submit(
      {.name = "prefab.prepare", .scope = candidate->scope_},
      [result = candidate->state_, files = host->asset_files(),
       uri = std::string(uri)](const engine::core::tasks::task_context& context) {
        result->result = engine::scene::prepare_prefab_description(
            files, uri, result->prepared, result->diagnostic,
            [&] { return context.stop_requested(); });
        auto state = result->result == gneiss::engine::core::result::success
                         ? engine::core::tasks::task_state::succeeded
                         : engine::core::tasks::task_state::failed;
        if (context.stop_requested()) {
          state = engine::core::tasks::task_state::cancelled;
        }
        return engine::core::tasks::task_outcome{.state = state};
      },
      task);
  if (submitted != engine::core::tasks::submit_result::success) {
    return gneiss::engine::core::result::not_ready;
  }
  output = std::move(candidate);
  return gneiss::engine::core::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}
void prefab_preparation::cancel() {
  cancelled_ = true;
  executor_.cancel_scope(scope_);
}
gneiss::engine::core::result
prefab_preparation::poll(engine::scene::prepared_scene_description& output, bool& complete) {
  complete = false;
  std::vector<engine::core::tasks::task_completion> completions;
  if (executor_.poll(scope_, completions, 1U) == 0U) {
    return gneiss::engine::core::result::success;
  }
  complete = true;
  if (cancelled_ ||
      completions.front().outcome.state == engine::core::tasks::task_state::cancelled) {
    return gneiss::engine::core::result::invalid_state;
  }
  if (state_->result == gneiss::engine::core::result::success) {
    output = std::move(state_->prepared);
  }
  return state_->result;
}
}
