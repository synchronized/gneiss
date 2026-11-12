// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstddef>
namespace gneiss::render {
/** 初始化时固定的任务数上限；不是 CPU/GPU 字节预算，两个字段必须非零。 */
struct queue_options {
  /** 等待消费的帧数，不包含正在执行的帧。 */
  std::size_t pending_frames{3};
  /** 已接受但未完成的命令数，包含正在执行及让出执行权的命令。 */
  std::size_t commands{12};
};
}
