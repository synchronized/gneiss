// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstddef>
namespace gneiss::render {
/** 初始化时固定的队列容量；任务数必须非零，字节预算可选。 */
struct queue_options {
  /** 等待消费的帧数，不包含正在执行的帧。 */
  std::size_t pending_frames{3};
  /** 已接受但未完成的命令数，包含正在执行及让出执行权的命令。 */
  std::size_t commands{12};
  /** 在途命令直接携带的数据预算；共享所有者去重，零表示不限制。不包含帧和 GPU 内存。 */
  std::size_t command_payload_bytes{};
  /** 已接受帧的视图/UI/调试输入预算，含执行及替换回收；零表示不限制。 */
  std::size_t frame_input_bytes{};
};
}
