// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/registry.hpp>
namespace gneiss {
/** 启动时依次注册内置类型、项目类型，再冻结默认注册表。
 * callback 同步调用且不可重入本函数，可使用默认 builder；预期失败返回错误，意外异常向执行边界传播。
 * 无 callback 的重复调用返回首次结果；启动后新增 callback 返回 invalid_state。
 * 失败结果保留至进程退出，不冻结半成品。初始化期间不得并行注册或手动冻结默认表。
 * 独立 registry 不受影响；应用关闭不会清空默认表。 */
using meta_registration_callback = error (*)();
[[nodiscard]] GNEISS_API error initialize_meta(meta_registration_callback callback = nullptr);
}
