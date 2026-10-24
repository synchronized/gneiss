// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PROPERTY_INSPECTOR_MODEL_H_
#define GNEISS_APPS_EDITOR_PROPERTY_INSPECTOR_MODEL_H_

#include "engine/core/entity_id.hpp"
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

#include <string>
#include <vector>

namespace gneiss::editor {

struct inspector_property final {
  field_id id{};
  std::string name;
  gneiss::engine::value_kind kind = gneiss::engine::value_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
  gneiss::engine::value value{};
};

struct inspector_component final {
  gneiss::engine::type_guid type_id{};
  std::string name;
  std::vector<inspector_property> properties;
};

/** 为编辑器提供与 UI 框架无关的反射属性快照和写入入口。 */
class property_inspector_model final {
public:
  property_inspector_model() = default;
  property_inspector_model(const property_inspector_model&) = delete;
  property_inspector_model& operator=(const property_inspector_model&) = delete;
  property_inspector_model(property_inspector_model&&) noexcept = default;
  property_inspector_model& operator=(property_inspector_model&&) noexcept = default;

  [[nodiscard]] gneiss::engine::result initialize() noexcept;
  void clear() noexcept;

  /** 刷新实体的已注册组件；实体不存在时清空旧快照。 */
  [[nodiscard]] gneiss::engine::result refresh(gneiss::engine::rid world,
                                               entity_id entity) noexcept;

  /** 通过 World 组件属性入口写入属性；失败时保留运行时值和当前快照。 */
  [[nodiscard]] gneiss::engine::result set_value(gneiss::engine::type_guid type_id,
                                                 field_id field_id,
                                                 const gneiss::engine::value& value) noexcept;

  /** 按稳定实体 ID 写入属性，供撤销/重做在选择变化后复用。 */
  [[nodiscard]] gneiss::engine::result set_value(gneiss::engine::rid world, entity_id entity,
                                                 gneiss::engine::type_guid type_id,
                                                 field_id field_id,
                                                 const gneiss::engine::value& value) noexcept;

  [[nodiscard]] const std::vector<inspector_component>& components() const noexcept {
    return components_;
  }

private:
  gneiss::engine::registry_ref registry_;
  gneiss::engine::rid world_{};
  entity_id entity_{};
  std::vector<inspector_component> components_;
};

} // namespace gneiss::editor

#endif
