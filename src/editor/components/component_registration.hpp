// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/scene/component_registration.hpp>
namespace gneiss::editor {
enum class component_editor_route { reflected, mesh_assets };
struct component_editor_info {
  gneiss::guid type;
  bool addable = false;
  bool removable = false;
  component_editor_route route = component_editor_route::reflected;
};
/** 编辑器专用策略；运行装配与描述反射不依赖本注册表。 */
struct component_editor_adapter {
  gneiss::guid type;
  std::function<gneiss::result<gneiss::component_description>()> create_default{};
  bool removable = false;
  component_editor_route route = component_editor_route::reflected;
  std::function<gneiss::result<gneiss::component_description>(gneiss::game_object,
                                                              const gneiss::component_description&)>
      prepare{};
};
/** 启动时注册，首次查询后冻结；运行适配器应单独注册。 */
gneiss::error register_component_editor(component_editor_adapter);
gneiss::result<component_editor_info> component_editor(gneiss::guid);
gneiss::result<std::vector<component_editor_info>> component_editor_types();
gneiss::result<gneiss::component_description> default_component_description(gneiss::guid);
gneiss::result<gneiss::component_description>
prepare_component_editor_value(gneiss::world&, entity_id, const gneiss::component_description&);
}
