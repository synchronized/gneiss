// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

namespace gneiss::editor {

/** 解析宿主启动参数、装配服务并运行 Editor；异常转换为进程退出码。 */
[[nodiscard]] int run_editor_application(int argc, char** argv) noexcept;

} // namespace gneiss::editor
