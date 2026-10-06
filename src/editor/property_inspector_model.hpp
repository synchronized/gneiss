// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PROPERTY_INSPECTOR_MODEL_H_
#define GNEISS_APPS_EDITOR_PROPERTY_INSPECTOR_MODEL_H_

#include <gneiss/reflection.hpp>
#include <gneiss/world.hpp>

#include <string>
#include <vector>

namespace gneiss::editor {

struct inspector_property final {
  gneiss_field_id id = GNEISS_NULL_FIELD_ID;
  std::string name;
  std::uint32_t kind = GNEISS_PROPERTY_KIND_INVALID;
  std::uint32_t capabilities = 0;
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
};

struct inspector_component final {
  gneiss_type_id type_id{};
  std::string name;
  std::vector<inspector_property> properties;
};

/** 为编辑器提供与 UI 框架无关的反射属性快照和写入入口。 */
class property_inspector_model final {
public:
  [[nodiscard]] result initialize() noexcept;
  void clear() noexcept;

  /** 刷新实体的已注册组件；实体不存在时清空旧快照。 */
  [[nodiscard]] result refresh(gneiss_world world, entity_id entity) noexcept;

  /** 通过 Registry 写入属性；失败时保留运行时值和当前快照。 */
  [[nodiscard]] result set_value(gneiss_type_id type_id, gneiss_field_id field_id,
                                 const gneiss_property_value& value) noexcept;

  /** 按稳定实体 ID 写入属性，供撤销/重做在选择变化后复用。 */
  [[nodiscard]] result set_value(gneiss_world world, entity_id entity, gneiss_type_id type_id,
                                 gneiss_field_id field_id,
                                 const gneiss_property_value& value) noexcept;

  [[nodiscard]] const std::vector<inspector_component>& components() const noexcept {
    return components_;
  }

private:
  type_registry registry_;
  gneiss_property_target target_{};
  std::vector<inspector_component> components_;
};

} // namespace gneiss::editor

#endif
