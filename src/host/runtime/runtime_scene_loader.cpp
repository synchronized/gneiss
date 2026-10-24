// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_loader.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"

namespace gneiss::host::runtime {
namespace {
ipc_scene_phase convert(::gneiss::runtime::scene_load_phase phase) {
  using source = ::gneiss::runtime::scene_load_phase;
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
    return "正在建立场景资源引用";
  case ipc_scene_phase::verifying:
    return "正在校验场景结构来源";
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
gneiss::engine::core::result runtime_scene_loader::request(ipc_scene_request source,
                                                           std::uint32_t request_id) {
  if (active_) {
    return gneiss::engine::core::result::not_ready;
  }
  if (source.session == progress_.source.session && source.revision <= progress_.source.revision) {
    return gneiss::engine::core::result::invalid_state;
  }
  std::uint64_t accepted{};
  const auto result =
      application_.request_scene(source.uri, source.session, source.revision, accepted);
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  request_ = accepted;
  request_id_ = request_id;
  result_ = gneiss::engine::core::result::success;
  progress_ = {.source = std::move(source), .can_cancel = true, .message = "正在准备场景描述"};
  active_ = true;
  return gneiss::engine::core::result::success;
}
gneiss::engine::core::result runtime_scene_loader::cancel(const ipc_scene_request& source) {
  if (!active_ || source.session != progress_.source.session ||
      source.revision != progress_.source.revision || source.uri != progress_.source.uri) {
    return gneiss::engine::core::result::not_ready;
  }
  const auto result = application_.cancel_scene(request_);
  if (result == gneiss::engine::core::result::success) {
    progress_.can_cancel = false;
    progress_.message = "正在安全回收加载候选";
  }
  return result;
}
gneiss::engine::core::result
runtime_scene_loader::activate(::gneiss::runtime::scene_load_completion& completion) {
  if (before_activate) {
    const auto stopped = before_activate();
    if (stopped != gneiss::engine::core::result::success) {
      return stopped;
    }
  }
  const auto result = application_.activate_scene(request_, completion);
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  // 激活后的任意模块副作用不在场景事务的自动回滚承诺内。
  return after_activate ? after_activate(completion) : gneiss::engine::core::result::success;
}
namespace {
ipc_scene_budget budget_snapshot(const ::gneiss::runtime::scene_load_progress& value) {
  return {
      .candidate_logical_bytes = value.resident_bytes,
      .candidate_cpu_data_bytes = value.cpu_data_bytes,
      .application_logical_bytes = value.application_logical_bytes,
      .application_cpu_data_bytes = value.application_cpu_data_bytes,
      .available_bytes = value.available_bytes,
      .upload_reserved_bytes = value.upload_reserved_bytes,
      .peak_upload_bytes = value.peak_upload_bytes,
      .cleanup_complete = value.cleanup_complete,
      .cleanup_pending = value.cleanup_pending,
  };
}
} // namespace
gneiss::engine::core::result runtime_scene_loader::advance() {
  using namespace ::gneiss::runtime;
  scene_load_completion completion;
  bool finished{};
  auto result = application_.poll_scene(completion, finished);
  if (result != gneiss::engine::core::result::success || !active_) {
    return result;
  }
  if (!finished) {
    scene_load_progress current;
    if (!application_.scene_progress(current)) {
      return gneiss::engine::core::result::success;
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
    if (current.phase == scene_load_phase::cancelled && current.cleanup_complete) {
      active_ = false;
    }
    if (current.phase != scene_load_phase::ready || !application_.can_activate_scene()) {
      return gneiss::engine::core::result::success;
    }
    result = activate(completion);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
  }
  active_ = completion.progress.cleanup_pending;
  result_ = completion.result;
  progress_.phase = convert(completion.progress.phase);
  progress_.budget = budget_snapshot(completion.progress);
  progress_.can_cancel = false;
  progress_.message = completion.message;
  if (progress_.message.empty()) {
    progress_.message = phase_message(progress_.phase);
  }
  return gneiss::engine::core::result::success;
}
} // namespace gneiss::host::runtime
