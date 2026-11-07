// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/context.hpp"
namespace gneiss::engine::serialization {
/** 捕获独立的数据树；内嵌引用按内容捕获，外部引用由注册策略转换为身份。 */
GNEISS_API result<engine::value, diagnostic> capture_value(const context&,
                                                           const meta::instance&) noexcept;
GNEISS_API result<engine::value, diagnostic> capture_value(const meta::instance&) noexcept;
/** 恢复到候选对象；失败可留下部分修改，提交与回滚由编辑操作控制。 */
GNEISS_API diagnostic apply_value(const context&, const meta::mutable_instance&,
                                  const engine::value&) noexcept;
GNEISS_API diagnostic apply_value(const meta::mutable_instance&, const engine::value&) noexcept;
}
