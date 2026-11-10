// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/component_adapter.hpp"
namespace gneiss::editor {
enum class component_editor_route { reflected, mesh_assets };
struct component_editor_info {
  engine::guid type;
  bool addable = false;
  bool removable = false;
  component_editor_route route = component_editor_route::reflected;
};
/** 编辑器专用策略；运行装配与描述反射不依赖本注册表。 */
struct component_editor_adapter {
  engine::guid type;
  std::function<engine::result<engine::component_description>()> create_default{};
  bool removable = false;
  component_editor_route route = component_editor_route::reflected;
  std::function<engine::result<engine::component_description>(engine::game_object,
                                                              const engine::component_description&)>
      prepare{};
};
/** 启动时注册，首次查询后冻结；运行适配器应单独注册。 */
engine::error register_component_editor(component_editor_adapter);
engine::result<component_editor_info> component_editor(engine::guid);
engine::result<std::vector<component_editor_info>> component_editor_types();
engine::result<engine::component_description> default_component_description(engine::guid);
engine::result<engine::component_description>
prepare_component_editor_value(engine::world&, scene_node_id, const engine::component_description&);
}
