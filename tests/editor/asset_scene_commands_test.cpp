// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include "commands/asset_scene_commands.hpp"
#include "engine/core/result.hpp"
#include "runtime/application_registry.hpp"

#include "engine/values/core/result.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
#include <gneiss/engine/application.h>

#include <string>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  const std::string assets = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = assets.data();
  desc.asset_root_length = static_cast<std::uint32_t>(assets.size());
  gneiss::test_support::application_owner app;
  gneiss_world world = GNEISS_NULL_WORLD;
  scene_document session;
  editor_command_history history;
  if (gneiss::engine::result{gneiss_application_create(&desc, app.put())}.failed() ||
      gneiss::engine::result{gneiss_application_get_world(app.get(), &world)}.failed() ||
      session
          .open(*gneiss::runtime::find_application(app.get()), world,
                "asset://scenes/triangle.scene.json")
          .failed()) {
    return 1;
  }
  const auto initial_count = session.nodes().size();
  const auto parent = session.nodes().front().node;
  constexpr auto mesh = "asset://models/triangle.mesh.json";
  constexpr auto material = "asset://materials/triangle.material.json";
  constexpr auto prefab = "asset://prefabs/test.prefab.json";
  const auto add_temporary_mesh = [&] {
    const std::string name = "Mesh";
    const std::string mesh_uri = mesh;
    const std::string material_uri = material;
    return add_mesh_asset(session, history, name,
                          {.mesh_uri = mesh_uri, .material_uri = material_uri});
  };
  if (add_temporary_mesh().failed() || session.nodes().size() != initial_count + 1U ||
      !session.is_dirty() || history.size() != 1U) {
    return 2;
  }
  const auto uuid = session.selected_node()->uuid;
  if (session.select(parent).failed() || history.undo().failed() ||
      session.find_node(uuid) != nullptr || history.redo().failed() ||
      session.find_node(uuid) == nullptr || session.find_node(uuid)->display_name != "Mesh") {
    return 3;
  }
  if (replace_mesh_assets(
          session, history, uuid,
          {.mesh_uri = mesh, .material_uri = "asset://materials/missing.material.json"})
          .ok() ||
      history.size() != 1U || session.find_node(uuid)->material_uri != material ||
      replace_mesh_assets(session, history, "missing",
                          {.mesh_uri = mesh, .material_uri = material}) !=
          gneiss::engine::result::not_found) {
    return 4;
  }
  if (replace_mesh_assets(session, history, uuid, {.mesh_uri = mesh, .material_uri = material})
          .failed() ||
      history.size() != 2U || history.undo().failed() || history.redo().failed()) {
    return 5;
  }
  const auto add_temporary_prefab = [&] {
    const std::string name = "Prefab";
    const std::string uri = prefab;
    return add_prefab_asset(session, history, name, uri, parent);
  };
  if (add_temporary_prefab().failed() || session.selected_prefab_node() == nullptr) {
    return 6;
  }
  const auto prefab_uuid = session.selected_prefab_node()->instance_uuid;
  if (history.undo().failed() || session.find_prefab_root(prefab_uuid) != nullptr ||
      history.redo().failed() || session.find_prefab_root(prefab_uuid) == nullptr ||
      session.find_prefab_root(prefab_uuid)->parent != parent) {
    return 7;
  }
  // 容量耗尽时撤回刚创建的节点/实例，且不能把干净文档标脏。
  editor_command_history full{0U};
  session.clear_dirty();
  const auto nodes = session.nodes().size();
  const auto prefabs = session.prefab_nodes().size();
  if (add_mesh_asset(session, full, "Rollback Mesh",
                     {.mesh_uri = mesh, .material_uri = material}) !=
          gneiss::engine::result::invalid_argument ||
      session.nodes().size() != nodes || session.is_dirty() ||
      add_prefab_asset(session, full, "Rollback Prefab", prefab, parent) !=
          gneiss::engine::result::invalid_argument ||
      session.prefab_nodes().size() != prefabs || session.is_dirty() || full.can_undo()) {
    return 8;
  }
  if (replace_mesh_assets(session, full, uuid, {.mesh_uri = mesh, .material_uri = material}) !=
          gneiss::engine::result::invalid_argument ||
      session.is_dirty() || session.find_node(uuid)->material_uri != material) {
    return 9;
  }
  history.clear();
  session.close();
  return 0;
} catch (...) {
  return 10;
}
