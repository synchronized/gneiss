// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/camera_settings.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::engine {
/** World 层相机属性快照；活动状态只读，不持有 World 或组件地址。 */
struct camera_properties {
  inline static constexpr std::string_view type_name = "camera";
  inline static constexpr engine::type_guid type_guid = camera_component_type;
  camera_settings settings{};
  bool is_primary{};
};

/** 创建冻结的内置组件注册表；失败保持输出，组件读写入口验证 World RID 和所属线程。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
create_property_registry(gneiss::engine::registry_ref&) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid transform_property_type() noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::type_guid camera_property_type() noexcept;
/** 在 World 所属线程定位内建组件；读出副本后访问，写入成功才提交。不会保留对象指针。
 * 失败保持既有组件；未知组件类型返回 unsupported。值及字符串由调用方持有。 */
[[nodiscard]] GNEISS_API result read_component_property(const registry_ref&, rid world,
                                                        std::uint64_t entity, type_guid, field_id,
                                                        gneiss::engine::value&) noexcept;
[[nodiscard]] GNEISS_API result write_component_property(const registry_ref&, rid world,
                                                         std::uint64_t entity, type_guid, field_id,
                                                         const gneiss::engine::value&) noexcept;
} // namespace gneiss::engine
