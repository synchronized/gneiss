// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include <memory>

namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
/** 单个 Prefab 纯描述准备；所属线程取得描述引用，工作任务只排序并收集依赖，不访问活动 World。
 * Application 须先通过 attach_task_executor 接入同一执行器及平台异步文件后端。
 * 所属线程操作。取消后仍须 poll 至完成；Application、资产管理器与执行器须晚于该对象销毁。
 */
class prefab_preparation final {
public:
  static GNEISS_API gneiss::engine::error
  create(runtime::application& application, gneiss::engine::task_executor& executor,
         std::string_view uri, std::unique_ptr<prefab_preparation>& output) noexcept;
  GNEISS_API ~prefab_preparation();
  GNEISS_API void cancel();
  GNEISS_API gneiss::engine::error poll(gneiss::engine::scene_instantiation_data& output,
                                        bool& complete);

private:
  struct state;
  GNEISS_API prefab_preparation(gneiss::engine::task_executor& executor);
  gneiss::engine::task_executor& executor_;
  gneiss::engine::task_scope scope_;
  std::shared_ptr<state> state_;
  bool cancelled_{};
};
}
