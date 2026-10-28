// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

namespace gneiss::engine {
[[nodiscard]] GNEISS_API error register_reflection(const meta::registry_ref& registry);
/** World 层相机属性快照；活动状态只读，不持有 World 或组件地址。 */
struct camera_properties {
  inline static constexpr std::string_view type_name = "camera";
  inline static constexpr engine::guid type_guid = camera_component_type;
  math::camera_settings settings{};
  bool is_primary{};
};

/** 创建冻结的内置组件注册表；失败保持输出，组件读写入口验证 World 实体有效性和所属线程。 */
[[nodiscard]] GNEISS_API error create_property_registry(meta::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API guid transform_property_type() noexcept;
[[nodiscard]] GNEISS_API guid camera_property_type() noexcept;
/** 在 World 所属线程定位内建组件；读出副本后访问，写入成功才提交。不会保留对象指针。
 * 失败保持既有组件；未知组件类型返回 unsupported。值及字符串由调用方持有。 */
[[nodiscard]] GNEISS_API error read_component_property(const meta::registry_ref&, world& world,
                                                       std::uint64_t entity, guid,
                                                       meta::property_id, value&) noexcept;
[[nodiscard]] GNEISS_API error write_component_property(const meta::registry_ref&, world& world,
                                                        std::uint64_t entity, guid,
                                                        meta::property_id, const value&) noexcept;
} // namespace gneiss::engine
