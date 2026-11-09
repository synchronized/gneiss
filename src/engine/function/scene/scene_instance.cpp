// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_instance_state.hpp"
#include "engine/function/scene/scene_types.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/function/world/world_state.hpp"

#include "engine/function/scene/scene_object.hpp"

#include "engine/resource/scene/scene_description.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace gneiss::engine {
namespace {
// 创建操作的临时输入；不会进入场景成员列表。
struct staged_object {
  gneiss::engine::guid uuid;
  std::uint64_t entity{};
  scene_node_id node{};
  ref<const engine::mesh> mesh;
  ref<const engine::material> material;
};
error commit_object(world& native_world, const game_object_description& source,
                    staged_object& target, scene_node_id parent, asset_manager& resources) {
  auto created =
      create_scene_object(native_world, source, parent, {target.mesh, target.material}, &resources);
  if (!created)
    return created.error();
  target.node = *created;
  target.entity = scene_object_entity(native_world, target.node);
  return error::success;
}

} // namespace
scene_instance_state::scene_instance_state(world& native_world, asset_manager& loader) noexcept
    : native_world_(native_world), loader_(loader) {}

scene_instance_state::~scene_instance_state() noexcept { rollback(); }

world_state& scene_instance_state::storage() const noexcept {
  auto state = detail::world_component_access::get(native_world_);
  if (!state)
    fatal(fatal_reason::unhandled_exception);
  return **state;
}
std::vector<scene_node_id> scene_instance_state::members() const {
  std::vector<scene_node_id> result;
  storage().each_scene_member(id_, [&](scene_node_id node) { result.push_back(node); });
  std::ranges::sort(result, {}, [](scene_node_id node) { return node.get(); });
  return result;
}
std::uint64_t scene_instance_state::node_count() const noexcept {
  std::uint64_t count{};
  storage().each_scene_member(id_, [&](scene_node_id) { ++count; });
  return count;
}
bool scene_instance_state::contains_editable_node(scene_node_id node) const noexcept {
  return id_ && storage().scene_of(node) == id_;
}
void scene_instance_state::adopt(scene_node_id node) {
  try {
    storage().assign_scene(node, id_);
  } catch (...) {
    (void)native_world_.object_destroy(node);
    throw;
  }
}
void scene_instance_state::rollback() noexcept try {
  storage().destroy_scene_members(id_);
  loader_.collect_unused();
} catch (...) {
  gneiss::engine::fatal_current_exception();
}
scene_node_id scene_instance_state::find_node(guid uuid) const {
  scene_node_id found;
  storage().each_scene_member(id_, [&](scene_node_id node) {
    if (scene_object_identity(native_world_, node).uuid == uuid)
      found = node;
  });
  return found;
}

result<scene_node_info> scene_instance_state::get_node_info(std::uint64_t index,
                                                            node_query_detail detail) const {
  const auto nodes = members();
  if (index >= nodes.size()) {
    return engine::error::not_found;
  }
  const auto node = nodes[static_cast<std::size_t>(index)];
  std::uint64_t entity = UINT64_C(0);
  auto result = engine::write_identifier<entity_id>(entity, [&](auto& native_output) {
    return native_world_.node_get_entity(gneiss::scene_node_id{node}, native_output);
  });
  if (result != engine::error::success || entity == UINT64_C(0)) {
    return result == engine::error::success ? engine::error::invalid_handle : result;
  }
  scene_node_id parent = null_scene_node_id;
  result = native_world_.node_get_parent(gneiss::scene_node_id{node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  scene_node_info out_info{};
  out_info.node = scene_node_id{node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = scene_object_identity(native_world_, node).uuid;
  out_info.name = scene_object_identity(native_world_, node).name;
  if (detail != node_query_detail::identity) {
    auto components = capture_components(native_world_, node);
    if (!components)
      return components.error();
    out_info.components = std::move(*components);
  }
  if (detail == node_query_detail::components) {
    result = native_world_.node_get_local_transform(node, out_info.local_transform);
    if (result.failed())
      return result;
  }
  return out_info;
}

result<scene_node_id> scene_instance_state::create_node(const node_creation& desc) {
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  if (!uuid || find_node(uuid) != null_scene_node_id) {
    return engine::error::invalid_argument;
  }
  gneiss::engine::guid parent_uuid{};
  if (desc.parent != null_scene_node_id) {
    if (!contains_editable_node(desc.parent)) {
      return engine::error::invalid_handle;
    }
    parent_uuid = scene_object_identity(native_world_, desc.parent).uuid;
  }
  {
    game_object_description author{.uuid = uuid,
                                   .name = std::string(name),
                                   .parent = {std::move(parent_uuid), {}},
                                   .transform = desc.local_transform,
                                   .components = {}};
    staged_object target{.uuid = author.uuid,
                         .entity = UINT64_C(0),
                         .node = null_scene_node_id,
                         .mesh = {},
                         .material = {}};
    const auto result = commit_object(native_world_, author, target, desc.parent, loader_);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    adopt(target.node);
    return target.node;
  }
}

engine::error scene_instance_state::reparent_node(scene_node_id node, scene_node_id parent) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  if (parent && !contains_editable_node(parent))
    return error::invalid_handle;
  return native_world_.node_reparent(node, parent);
}

engine::error scene_instance_state::destroy_subtree(scene_node_id root) {
  if (!contains_editable_node(root))
    return error::invalid_handle;
  const auto status = native_world_.object_destroy(root);
  if (status.ok())
    loader_.collect_unused();
  return status;
}

result<scene_node_id>
scene_instance_state::create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                                const scene_mesh_binding* prepared) {
  if (prepared &&
      (engine::resource_kind(prepared->mesh) != engine::render_resource_kind::mesh ||
       engine::resource_kind(prepared->material) != engine::render_resource_kind::material ||
       !loader_.owns(prepared->mesh) || !loader_.owns(prepared->material) ||
       engine::resource_uri(prepared->mesh) != desc.mesh_uri ||
       engine::resource_uri(prepared->material) != desc.material_uri)) {
    return engine::error::invalid_argument;
  }
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  const auto mesh_uri = desc.mesh_uri;
  const auto material_uri = desc.material_uri;
  if (!uuid || find_node(uuid) != null_scene_node_id) {
    return engine::error::invalid_argument;
  }
  gneiss::engine::guid parent_uuid{};
  if (desc.parent != null_scene_node_id) {
    if (!contains_editable_node(desc.parent)) {
      return engine::error::invalid_handle;
    }
    parent_uuid = scene_object_identity(native_world_, desc.parent).uuid;
  }

  {
    game_object_description author{
        .uuid = uuid,
        .name = std::string(name),
        .parent = {std::move(parent_uuid), {}},
        .transform = {},
        .components = {make_component_description(
            engine::mesh_renderer_description{std::string(mesh_uri), std::string(material_uri)})}};
    if (desc.component_uuid) {
      auto entries = author.components.entries();
      auto identified = with_component_identity(entries.front(), desc.component_uuid);
      if (!identified)
        return identified.error();
      entries.front() = std::move(*identified);
      auto status = author.components.replace(std::move(entries));
      if (status.failed())
        return status;
    }
    staged_object target{};
    target.uuid = author.uuid;

    auto result = engine::error::success;
    if (prepared) {
      target.mesh = prepared->mesh;
      target.material = prepared->material;
    } else {
      result = engine::error::success;
      {
        auto loaded = loader_.find<engine::mesh>(mesh_uri);
        if (loaded)
          target.mesh = std::move(*loaded);
        else {
          target.mesh = {};
          result = loaded.error();
        }
      };
      if (result == engine::error::success) {
        result = engine::error::success;
        {
          auto loaded = loader_.find<engine::material>(material_uri);
          if (loaded)
            target.material = std::move(*loaded);
          else {
            target.material = {};
            result = loaded.error();
          }
        };
      }
    }
    if (result != engine::error::success) {
      return result;
    }
    result = commit_object(native_world_, author, target, desc.parent, loader_);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    adopt(target.node);
    return target.node;
  }
}

engine::error scene_instance_state::set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                                      std::string_view material_uri) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  {
    auto current = native_world_.node_mesh_renderer(node);
    if (!current && current.error() != error::not_found)
      return current.error();
    const auto binding = current ? *current : mesh_renderer{};
    // 编辑资产选择不等于重载；未改变的 URI 继续使用节点选定的版本。
    auto mesh =
        engine::resource_uri(binding.mesh) == mesh_uri ? binding.mesh : ref<const engine::mesh>{};
    auto material = engine::resource_uri(binding.material) == material_uri
                        ? binding.material
                        : ref<const engine::material>{};

    auto result = engine::error::success;
    if (!mesh && !mesh_uri.empty()) {
      result = engine::error::success;
      {
        auto loaded = loader_.find<engine::mesh>(mesh_uri);
        if (loaded)
          mesh = std::move(*loaded);
        else {
          mesh = {};
          result = loaded.error();
        }
      };
    }
    if (result == engine::error::success && !material && !material_uri.empty()) {
      result = engine::error::success;
      {
        auto loaded = loader_.find<engine::material>(material_uri);
        if (loaded)
          material = std::move(*loaded);
        else {
          material = {};
          result = loaded.error();
        }
      };
    }
    if (result != engine::error::success) {
      return result;
    }
    const engine::mesh_renderer renderer{.mesh = mesh, .material = material};
    result = native_world_.entity_set_mesh_renderer(
        gneiss::entity_id{scene_object_entity(native_world_, node)}, renderer);
    if (result != engine::error::success) {
      return result;
    }
    loader_.collect_unused();
    return engine::error::success;
  }
}

engine::error scene_instance_state::set_camera(scene_node_id node,
                                               const engine::camera_description& desc) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  const auto result = native_world_.entity_configure_camera(
      gneiss::entity_id{scene_object_entity(native_world_, node)}, desc.settings);
  if (result != engine::error::success) {
    return result;
  }
  if (desc.is_primary) {
    const auto active_result = native_world_.set_active_camera(
        gneiss::entity_id{scene_object_entity(native_world_, node)});
    if (active_result != engine::error::success) {
      return active_result;
    }
  }
  return engine::error::success;
}

engine::error scene_instance_state::remove_camera(scene_node_id node) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  return native_world_.entity_remove_camera(
      gneiss::entity_id{scene_object_entity(native_world_, node)});
}

engine::error scene_instance_state::remove_mesh_renderer(scene_node_id node) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  const auto result = native_world_.entity_remove_mesh_renderer(
      gneiss::entity_id{scene_object_entity(native_world_, node)});
  if (result != engine::error::success) {
    return result;
  }
  loader_.collect_unused();
  return engine::error::success;
}

engine::error scene_instance_state::destroy_node(scene_node_id node) {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  const auto* children = storage().scene().children(node);
  if (children && !children->empty())
    return error::invalid_state;
  return destroy_subtree(node);
}

} // namespace gneiss::engine
