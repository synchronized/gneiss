// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/property_registry.hpp"

namespace gneiss::world_internal {
/** 创建冻结的内置组件注册表；失败保持输出，访问器仍验证 World RID 和所属线程。 */
[[nodiscard]] GNEISS_API result
create_property_registry(reflection_internal::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API type_id transform_property_type() noexcept;
[[nodiscard]] GNEISS_API type_id camera_property_type() noexcept;
} // namespace gneiss::world_internal
