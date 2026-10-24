// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"

namespace gneiss::engine {
/** 创建冻结的内置组件注册表；失败保持输出，访问器仍验证 World RID 和所属线程。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
create_property_registry(gneiss::engine::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid transform_property_type() noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid camera_property_type() noexcept;
} // namespace gneiss::engine
