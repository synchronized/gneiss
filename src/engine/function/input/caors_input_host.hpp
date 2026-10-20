// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "action_map.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include <functional>
#include <gneiss/engine/core/result.hpp>
namespace gneiss::tasks {
class task_executor;
}
namespace gneiss::input_internal {
/** asset_manager 私有输入加载宿主；借用调度器和 VFS 后端 I/O，析构先排空专属 scope。
 * 全部入口限所属主线程，后台不持有 InputService。 */
class caors_input_host final {
public:
  caors_input_host(asset_internal::virtual_file_system files, tasks::task_executor& executor);
  ~caors_input_host();
  result begin(std::string_view uri) noexcept;
  void cancel() noexcept;
  result status() const noexcept;
  void advance(const std::function<result(action_map)>& apply);

private:
  struct storage;
  std::unique_ptr<storage> storage_;
};
}
