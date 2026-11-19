// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/platform/window/configuration.hpp>

#include <string_view>

namespace gneiss::tasks {
class executor;
}

namespace gneiss::runtime {

/** 原生创建配置；字符串仅在 create 期间借用，Application 复制后用于异步启动。 */
struct application_configuration final {
  /** 借用资产任务执行器，必须覆盖 Application 关闭。空表示不装配资产任务。 */
  tasks::executor* executor{};
  bool enable_window = false;
  gneiss::window::configuration window;
  std::string_view asset_root;
  /** 编辑器显式允许单文件保存；Runtime 默认只读，远程 Fetch 不支持写入。 */
  bool writable_assets = false;
  /** 宿主准备的用户配置目录；非空时挂载可写 user://，不属于资产来源。 */
  std::string_view user_root;
};

} // namespace gneiss::runtime
