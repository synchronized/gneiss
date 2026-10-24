// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include <gneiss/engine/game_module_types.h>
#include <memory>

namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
/** 单个 Prefab 纯描述准备；任务只持有 VFS 副本与结果，不访问活动 World。
 * 所属线程操作。取消后仍须 poll 至完成；Application 的文件后端与执行器须晚于该对象销毁。
 */
class GNEISS_API prefab_preparation final {
public:
  static gneiss::engine::result create(runtime::application& application,
                                       gneiss::engine::task_executor& executor,
                                       std::string_view uri,
                                       std::unique_ptr<prefab_preparation>& output) noexcept;
  ~prefab_preparation();
  void cancel();
  gneiss::engine::result poll(gneiss::engine::prepared_scene_description& output, bool& complete);

private:
  struct state;
  prefab_preparation(gneiss::engine::task_executor& executor);
  gneiss::engine::task_executor& executor_;
  gneiss::engine::task_scope scope_;
  std::shared_ptr<state> state_;
  bool cancelled_{};
};
}
