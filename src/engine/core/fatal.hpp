// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

namespace gneiss::engine {
enum class fatal_reason { out_of_memory, unhandled_exception };
/** 尽力输出固定诊断后终止进程；不分配内存、不调用日志或用户回调，不承诺析构。 */
[[noreturn]] void fatal(fatal_reason reason) noexcept;
/** 执行边界使用：识别当前异常，随后终止；没有活动异常时按契约违例处理。 */
[[noreturn]] void fatal_current_exception() noexcept;
/** 仅由自有宿主显式调用；嵌入方自行管理进程级 terminate handler。 */
void install_fatal_handler() noexcept;
}
