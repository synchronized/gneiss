// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_loader.h"

namespace gneiss::runtime_internal {
namespace {
ipc_scene_phase convert(application_internal::scene_load_phase phase) {
  using source = application_internal::scene_load_phase;
  switch (phase) {
  case source::preparing:
    return ipc_scene_phase::preparing;
  case source::assets:
    return ipc_scene_phase::assets;
  case source::verifying:
    return ipc_scene_phase::verifying;
  case source::instantiating:
    return ipc_scene_phase::instantiating;
  case source::ready:
    return ipc_scene_phase::ready;
  case source::applied:
    return ipc_scene_phase::applied;
  case source::failed:
    return ipc_scene_phase::failed;
  case source::cancelled:
    return ipc_scene_phase::cancelled;
  }
  return ipc_scene_phase::failed;
}
const char* phase_message(ipc_scene_phase phase) {
  switch (phase) {
  case ipc_scene_phase::preparing:
    return "正在准备场景描述";
  case ipc_scene_phase::assets:
    return "正在准备场景资产";
  case ipc_scene_phase::verifying:
    return "正在验证源文件版本";
  case ipc_scene_phase::instantiating:
    return "正在构造候选场景";
  case ipc_scene_phase::ready:
    return "等待安全点激活";
  case ipc_scene_phase::applied:
    return "场景已激活";
  case ipc_scene_phase::failed:
    return "场景加载失败，保留原场景";
  case ipc_scene_phase::cancelled:
    return "场景加载已取消";
  }
  return "未知场景加载状态";
}

}
gneiss_result runtime_scene_loader::request(ipc_scene_request source, std::uint32_t request_id) {
  if (active_) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (source.session == progress_.source.session && source.revision <= progress_.source.revision) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  std::uint64_t accepted{};
  const auto result = application_internal::request_scene_load(
      application_, source.uri, source.session, source.revision, accepted);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  request_ = accepted;
  request_id_ = request_id;
  result_ = GNEISS_SUCCESS;
  progress_ = {.source = std::move(source), .can_cancel = true, .message = "正在准备场景描述"};
  active_ = true;
  return GNEISS_SUCCESS;
}
gneiss_result runtime_scene_loader::cancel(const ipc_scene_request& source) {
  if (!active_ || source.session != progress_.source.session ||
      source.revision != progress_.source.revision || source.uri != progress_.source.uri) {
    return GNEISS_ERROR_NOT_READY;
  }
  const auto result = application_internal::cancel_scene_load(application_, request_);
  if (result == GNEISS_SUCCESS) {
    progress_.can_cancel = false;
    progress_.message = "正在安全回收加载候选";
  }
  return result;
}
gneiss_result
runtime_scene_loader::activate(application_internal::scene_load_completion& completion) {
  if (before_activate) {
    const auto stopped = before_activate();
    if (stopped != GNEISS_SUCCESS) {
      return stopped;
    }
  }
  const auto result = application_internal::activate_scene_load(application_, request_, completion);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  // 激活后的任意模块副作用不在场景事务的自动回滚承诺内。
  return after_activate ? after_activate(completion) : GNEISS_SUCCESS;
}
namespace {
ipc_scene_budget budget_snapshot(const application_internal::scene_load_progress& value) {
  return {
      .candidate_logical_bytes = value.resident_bytes,
      .candidate_cpu_data_bytes = value.cpu_data_bytes,
      .application_logical_bytes = value.application_logical_bytes,
      .application_cpu_data_bytes = value.application_cpu_data_bytes,
      .available_bytes = value.available_bytes,
      .upload_reserved_bytes = value.upload_reserved_bytes,
      .peak_upload_bytes = value.peak_upload_bytes,
  };
}
} // namespace
gneiss_result runtime_scene_loader::advance() {
  using namespace application_internal;
  scene_load_completion completion;
  bool finished{};
  auto result = poll_scene_load(application_, completion, finished);
  if (result != GNEISS_SUCCESS || !active_) {
    return result;
  }
  if (!finished) {
    scene_load_progress current;
    bool available{};
    result = query_scene_load_progress(application_, current, available);
    if (result != GNEISS_SUCCESS || !available) {
      return result;
    }
    const auto phase = convert(current.phase);
    if (progress_.phase != phase) {
      progress_.message = phase_message(phase);
    }
    progress_.phase = phase;
    progress_.completed = static_cast<std::uint32_t>(current.completed);
    progress_.total = static_cast<std::uint32_t>(current.total);
    progress_.can_cancel = current.can_cancel;
    progress_.budget = budget_snapshot(current);
    if (current.phase != scene_load_phase::ready) {
      return GNEISS_SUCCESS;
    }
    result = activate(completion);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  active_ = false;
  result_ = completion.result;
  progress_.phase = convert(completion.progress.phase);
  progress_.budget = budget_snapshot(completion.progress);
  progress_.can_cancel = false;
  progress_.message = completion.message;
  if (progress_.message.empty()) {
    progress_.message = phase_message(progress_.phase);
  }
  return GNEISS_SUCCESS;
}
} // namespace gneiss::runtime_internal
