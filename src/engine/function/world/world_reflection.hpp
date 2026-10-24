// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/camera_settings.hpp"
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::engine {
/** World 层相机属性快照；活动状态只读，不持有 World 或组件地址。 */
struct camera_properties {
  camera_settings settings{};
  bool is_primary{};
};

/** 创建冻结的内置组件注册表；失败保持输出，组件读写入口验证 World RID 和所属线程。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
create_property_registry(gneiss::engine::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid transform_property_type() noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid camera_property_type() noexcept;
/** 在 World 所属线程定位内建组件；读出副本后访问，写入成功才提交。不会保留对象指针。
 * 失败保持既有组件；未知组件类型返回 unsupported。借用属性值入口仅服务内建无文本字段。 */
[[nodiscard]] GNEISS_API result read_component_property(const registry_ref&, rid world,
                                                        std::uint64_t entity, type_guid, field_id,
                                                        property_value&) noexcept;
[[nodiscard]] GNEISS_API result write_component_property(const registry_ref&, rid world,
                                                         std::uint64_t entity, type_guid, field_id,
                                                         const property_value&) noexcept;
[[nodiscard]] GNEISS_API result read_component_property(const registry_ref&, rid world,
                                                        std::uint64_t entity, type_guid, field_id,
                                                        gneiss::property_value&) noexcept;
[[nodiscard]] GNEISS_API result write_component_property(const registry_ref&, rid world,
                                                         std::uint64_t entity, type_guid, field_id,
                                                         const gneiss::property_value&) noexcept;
} // namespace gneiss::engine
