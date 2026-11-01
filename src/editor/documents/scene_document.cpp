// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/scene_document.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include "runtime/application.hpp"

#include "engine/function/world/component_identity.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <random>
#include <system_error>
#include <utility>

namespace gneiss::editor {
namespace {
[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& path) noexcept {
  auto root_part = root.begin();
  auto path_part = path.begin();
  for (; root_part != root.end(); ++root_part, ++path_part) {
    if (path_part == path.end() || *root_part != *path_part) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] gneiss::engine::guid make_uuid_value() {
  std::array<std::uint8_t, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<std::uint8_t>(random());
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
  return engine::guid::from_bytes(bytes);
}

} // namespace

gneiss::engine::error make_editor_uuid(engine::guid& output) noexcept {
  try {
    output = make_uuid_value();
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error make_asset_uri(const std::filesystem::path& asset_root,
                                     const std::filesystem::path& path,
                                     std::string& output) noexcept {
  if (asset_root.empty() || path.empty()) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::error_code error;
    const auto root = std::filesystem::canonical(asset_root, error);
    if (error) {
      return gneiss::engine::error::io;
    }
    const auto target = std::filesystem::weakly_canonical(path, error);
    if (error || !is_within(root, target)) {
      return gneiss::engine::error::invalid_argument;
    }
    const auto relative = std::filesystem::relative(target, root, error).generic_u8string();
    if (error || relative.empty()) {
      return gneiss::engine::error::invalid_argument;
    }
    std::string uri = "asset://";
    uri.append(reinterpret_cast<const char*>(relative.data()), relative.size());
    if (gneiss::engine::validate_uri(uri) != gneiss::engine::error::success) {
      return gneiss::engine::error::invalid_argument;
    }
    output = std::move(uri);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::io;
  }
}

bool scene_document::is_open() const noexcept {
  if (!application_ || !scene_ || !application_->is_owner_thread() ||
      !application_->accepts_requests() || application_->native_world() != world_)
    return false;
  // 地址可能被后续 World 复用；实例 ID 的所属域仍必须匹配当前实例集合。
  auto* scenes = application_->scenes();
  return scenes && scenes->get_node_count(scene_).ok();
}
template <class F>
std::invoke_result_t<F, engine::scene_instances&>
scene_document::with_scene(F&& action) const noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  auto* service = application_->scenes();
  if (!service)
    return engine::error::invalid_state;
  return std::forward<F>(action)(*service);
}

scene_document::scene_document(scene_document&& other) noexcept { *this = std::move(other); }
scene_document& scene_document::operator=(scene_document&& other) noexcept {
  if (this != &other) {
    close();
    application_ = std::exchange(other.application_, nullptr);
    world_ = std::exchange(other.world_, nullptr);
    scene_ = std::exchange(other.scene_, scene_instance_id{});
    selection_ = std::exchange(other.selection_, {});
    uri_ = std::move(other.uri_);
    is_dirty_ = std::exchange(other.is_dirty_, false);
  }
  return *this;
}

gneiss::engine::error scene_document::adopt(runtime::application& application,
                                            gneiss::engine::world* world,
                                            gneiss::scene_instance_id scene,
                                            std::string_view uri) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (world != application.native_world() || uri.empty() || !scene)
    return gneiss::engine::error::invalid_argument;
  try {
    scene_document pending;
    pending.uri_ = uri;
    pending.application_ = &application;
    pending.world_ = world;
    pending.scene_ = scene;
    const auto refreshed = pending.is_open() ? engine::error{engine::error::success}
                                             : engine::error{engine::error::invalid_handle};
    if (refreshed != gneiss::engine::error::success) {
      // 失败时仍由调用方持有场景，不改变当前文档。
      pending.scene_ = {};
      return refreshed;
    }
    *this = std::move(pending);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error scene_document::create_empty(runtime::application& application,
                                                   gneiss::engine::world* world,
                                                   gneiss::engine::guid scene_uuid) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (world != application.native_world() || scene_uuid.is_nil())
    return gneiss::engine::error::invalid_argument;
  auto created = application.scenes()->create_empty(scene_uuid);
  if (!created)
    return created.error();
  close();
  application_ = &application;
  scene_ = *created;
  world_ = world;
  is_dirty_ = true;
  return gneiss::engine::error::success;
}

gneiss::engine::error scene_document::create_empty(runtime::application& application,
                                                   gneiss::engine::world* world) noexcept {
  try {
    return create_empty(application, world, make_uuid_value());
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

namespace {
scene_node_record node_record(engine::scene_node_info info) {
  return {.node = info.node,
          .parent = info.parent,
          .entity = info.entity,
          .uuid = info.uuid,
          .display_name = info.name.empty() ? info.uuid.string() : std::string(info.name),
          .local_transform = info.local_transform,
          .components = std::move(info.components)};
}
prefab_node_record prefab_record(const engine::scene_node_info& info) {
  const auto& origin = *info.prefab;
  prefab_node_record record{
      .node = info.node,
      .parent = info.parent,
      .entity = info.entity,
      .instance_uuid = origin.instance_uuid,
      .source_node_uuid = origin.source_node_uuid,
      .display_name =
          info.name.empty()
              ? (origin.is_instance_root ? origin.instance_uuid : origin.source_node_uuid).string()
              : std::string(info.name),
      .prefab_uri = std::string(origin.prefab_uri),
      .local_transform = info.local_transform,
      .source_local_transform = origin.source_local_transform,
      .is_instance_root = origin.is_instance_root};
  for (const auto& item : origin.overrides)
    if (item.key.source_node_uuid == origin.source_node_uuid)
      record.overrides.push_back(item.key);
  return record;
}
}
template <class F>
engine::result<engine::scene_node_info> scene_document::query_info(F&& predicate) const noexcept {
  return with_scene([&](auto& scenes) -> engine::result<engine::scene_node_info> {
    auto count = scenes.get_node_count(scene_);
    if (!count)
      return count.error();
    for (std::uint64_t index = 0; index < *count; ++index) {
      auto info = scenes.get_node_info(scene_, index, engine::node_query_detail::identity);
      if (!info)
        return info.error();
      if (predicate(*info))
        return scenes.get_node_info(scene_, index);
    }
    return engine::error::not_found;
  });
}
engine::result<scene_query_snapshot> scene_document::capture_nodes() const noexcept {
  try {
    std::uint64_t count = 0;
    auto count_result = with_scene([&](auto& service) { return service.get_node_count(scene_); });
    if (!count_result)
      return count_result.error();
    count = std::move(*count_result);

    std::vector<scene_node_record> records;
    records.reserve(static_cast<std::size_t>(count));
    std::vector<prefab_node_record> prefab_records;
    prefab_records.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      gneiss::engine::scene_node_info info{};
      auto info_result =
          with_scene([&](auto& service) { return service.get_node_info(scene_, index); });
      if (!info_result)
        return info_result.error();
      info = std::move(*info_result);

      if (info.prefab)
        prefab_records.push_back(prefab_record(info));
      else
        records.push_back(node_record(std::move(info)));
    }
    return scene_query_snapshot{std::move(records), std::move(prefab_records)};
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error
scene_document::get_world_transform(scene_node_id node,
                                    gneiss::engine::math::transform& output) const noexcept {
  gneiss::engine::math::transform value;
  const auto operation = with_scene(
      [&](auto&) { return application_->native_world()->node_get_world_transform(node, value); });
  if (operation.ok())
    output = value;
  return operation;
}

gneiss::engine::error
scene_document::set_local_transform(scene_node_id node,
                                    const gneiss::engine::math::transform& value) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  auto info = query_info([&](const auto& info) { return info.node == node; });
  if (!info)
    return info.error();
  const auto operation = info->prefab && !info->prefab->is_instance_root
                             ? with_scene([&](auto& scenes) {
                                 return scenes.set_prefab_source_transform(scene_, node, value);
                               })
                             : world_->node_set_local_transform(node, value);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return gneiss::engine::error::success;
}

gneiss::engine::error scene_document::restore_prefab_transform_field(
    scene_node_id node, std::uint32_t field_id,
    gneiss::engine::math::transform& out_previous) noexcept {
  const auto found = query_prefab_node(node);
  if (!found || found->is_instance_root) {
    return gneiss::engine::error::invalid_argument;
  }
  auto restored = found->local_transform;
  switch (field_id) {
  case gneiss::engine::transform_translation_field.get():
    restored.translation = found->source_local_transform.translation;
    break;
  case gneiss::engine::transform_rotation_field.get():
    restored.rotation = found->source_local_transform.rotation;
    break;
  case gneiss::engine::transform_scale_field.get():
    restored.scale = found->source_local_transform.scale;
    break;
  default:
    return gneiss::engine::error::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, restored);
  if (operation == gneiss::engine::error::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::engine::error
scene_document::restore_prefab_transform(scene_node_id node,
                                         gneiss::engine::math::transform& out_previous) noexcept {
  const auto found = query_prefab_node(node);
  if (!found || found->is_instance_root) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, found->source_local_transform);
  if (operation == gneiss::engine::error::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::engine::error scene_document::create_node(std::string_view name, scene_node_id parent,
                                                  scene_node_id& out_node) noexcept {
  if (!is_open()) {
    return gneiss::engine::error::invalid_state;
  }
  try {
    const auto uuid = make_uuid_value();
    gneiss::engine::node_creation desc{};
    desc.uuid = uuid;
    desc.name = name;
    desc.parent = parent;
    auto out_node_result =
        with_scene([&](auto& service) { return service.create_node(scene_, desc); });
    out_node = {};
    if (!out_node_result)
      return out_node_result.error();
    out_node = *out_node_result;
    selection_ = out_node;
    is_dirty_ = true;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error scene_document::rename_node(scene_node_id node,
                                                  std::string_view name) noexcept {
  if (application_ && !application_->is_owner_thread()) {
    return gneiss::engine::error::invalid_state;
  }
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto operation = with_scene([&](auto& service) {
    return service.set_node_name(scene_, gneiss::scene_node_id{node}, name);
  });
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return engine::error::success;
}

gneiss::engine::error scene_document::reparent_node(scene_node_id node,
                                                    scene_node_id parent) noexcept {
  if (!is_open() || !node.is_valid() || node == parent) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto operation = with_scene([&](auto& service) {
    return service.reparent_node(scene_, gneiss::scene_node_id{node},
                                 gneiss::scene_node_id{parent});
  });
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return engine::error::success;
}

engine::error scene_document::destroy_subtree(scene_node_id node,
                                              scene_subtree_snapshot& output) noexcept try {
  scene_subtree_snapshot snapshot;
  auto snapshot_result =
      with_scene([&](auto& service) { return service.capture_subtree(scene_, node); });
  if (!snapshot_result)
    return snapshot_result.error();
  snapshot = std::move(*snapshot_result);
  auto status = with_scene([&](auto& service) { return service.destroy_subtree(scene_, node); });
  if (status.failed())
    return status;
  output = std::move(snapshot);
  selection_ = {};
  is_dirty_ = true;
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_document::restore_subtree(const scene_subtree_snapshot& snapshot,
                                              scene_node_id& output) noexcept {
  scene_node_id parent;
  if (!snapshot.parent_uuid.is_nil()) {
    auto parent_result =
        with_scene([&](auto& service) { return service.find_node(scene_, snapshot.parent_uuid); });
    if (!parent_result)
      return parent_result.error();
    parent = std::move(*parent_result);
  }
  auto output_result = with_scene(
      [&](auto& service) { return service.restore_subtree(scene_, snapshot, parent, {}); });
  output = {};
  if (!output_result)
    return output_result.error();
  output = *output_result;
  selection_ = output;
  is_dirty_ = true;
  return engine::error::success;
}

engine::error scene_document::duplicate_subtree(scene_node_id node, scene_node_id parent,
                                                scene_node_id& output) noexcept try {
  scene_subtree_snapshot snapshot;
  auto snapshot_result =
      with_scene([&](auto& service) { return service.capture_subtree(scene_, node); });
  if (!snapshot_result)
    return snapshot_result.error();
  snapshot = std::move(*snapshot_result);
  std::vector<engine::guid> targets;
  targets.reserve(snapshot.description.objects.size());
  for (std::size_t i = 0; i < snapshot.description.objects.size(); ++i)
    targets.push_back(make_uuid_value());
  std::vector<engine::uuid_mapping> mappings;
  for (std::size_t i = 0; i < targets.size(); ++i)
    mappings.push_back({snapshot.description.objects[i].uuid, targets[i]});
  auto output_result = with_scene(
      [&](auto& service) { return service.restore_subtree(scene_, snapshot, parent, mappings); });
  output = {};
  if (!output_result)
    return output_result.error();
  output = *output_result;
  selection_ = output;
  is_dirty_ = true;
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::result<engine::component_descriptions>
scene_document::components(scene_node_id node) const noexcept {
  auto status =
      with_scene([&](auto& service) { return service.validate_editable_node(scene_, node); });
  if (status.failed())
    return status;
  return engine::capture_components(*world_, node);
}
engine::result<engine::prefab_component_state>
scene_document::capture_component_override(scene_node_id node, engine::guid type) noexcept {
  return with_scene(
      [&](auto& service) { return service.capture_component_override(scene_, node, type); });
}
engine::error
scene_document::set_component_override(scene_node_id node, engine::guid type,
                                       const engine::prefab_component_state& state) noexcept {
  auto status = with_scene(
      [&](auto& service) { return service.set_component_override(scene_, node, type, state); });
  if (status.failed())
    return status;
  is_dirty_ = true;
  return engine::error::success;
}
engine::error
scene_document::add_component_description(scene_node_id node,
                                          const engine::component_description& value) noexcept {
  auto status = with_scene(
      [&](auto& service) { return service.add_component_description(scene_, node, value); });
  if (status.failed())
    return status;
  is_dirty_ = true;
  return engine::error::success;
}
engine::error scene_document::remove_component_description(scene_node_id node,
                                                           engine::guid type) noexcept {
  auto status = with_scene(
      [&](auto& service) { return service.remove_component_description(scene_, node, type); });
  if (status.failed())
    return status;
  is_dirty_ = true;
  return engine::error::success;
}
engine::error
scene_document::set_component_description(scene_node_id node,
                                          const engine::component_description& value) noexcept {
  auto status = with_scene(
      [&](auto& service) { return service.set_component_description(scene_, node, value); });
  if (status.failed())
    return status;
  is_dirty_ = true;
  return engine::error::success;
}

gneiss::engine::error
scene_document::set_camera(scene_node_id node,
                           const gneiss::engine::camera_description& desc) noexcept {
  const auto operation = with_scene(
      [&](auto& service) { return service.set_camera(scene_, gneiss::scene_node_id{node}, desc); });
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return engine::error::success;
}

gneiss::engine::error scene_document::remove_camera(scene_node_id node) noexcept {
  const auto operation = with_scene(
      [&](auto& service) { return service.remove_camera(scene_, gneiss::scene_node_id{node}); });
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return engine::error::success;
}

gneiss::engine::error scene_document::remove_mesh_renderer(scene_node_id node) noexcept {
  const auto operation = with_scene([&](auto& service) {
    return service.remove_mesh_renderer(scene_, gneiss::scene_node_id{node});
  });
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  is_dirty_ = true;
  return engine::error::success;
}

void scene_document::close() noexcept {
  selection_ = {};
  uri_.clear();
  is_dirty_ = false;
  if (is_open())
    (void)with_scene([&](auto& service) { return service.unload(scene_); });
  scene_ = {};
  application_ = nullptr;
  world_ = 0;
}

engine::result<scene_node_record> scene_document::query_node(scene_node_id node) const noexcept
    try {
  auto info = query_info([&](const auto& info) { return !info.prefab && info.node == node; });
  if (!info)
    return info.error();
  return node_record(std::move(*info));
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_document::query_prefab_node(scene_node_id node) const noexcept try {
  auto info = query_info([&](const auto& info) { return info.prefab && info.node == node; });
  if (!info)
    return info.error();
  return prefab_record(*info);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_node_record> scene_document::find_node(engine::guid uuid) const noexcept try {
  auto info = query_info([&](const auto& info) { return !info.prefab && info.uuid == uuid; });
  if (!info)
    return info.error();
  return node_record(std::move(*info));
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_document::find_prefab_root(engine::guid uuid) const noexcept try {
  auto info = query_info([&](const auto& info) {
    return info.prefab && info.prefab->instance_uuid == uuid && info.prefab->is_instance_root;
  });
  if (!info)
    return info.error();
  return prefab_record(*info);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_document::find_prefab_source(engine::guid uuid, engine::guid source) const noexcept try {
  auto info = query_info([&](const auto& info) {
    return info.prefab && info.prefab->instance_uuid == uuid &&
           info.prefab->source_node_uuid == source;
  });
  if (!info)
    return info.error();
  return prefab_record(*info);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_node_record> scene_document::selected_node() const noexcept {
  return query_node(selection_);
}
engine::result<prefab_node_record> scene_document::selected_prefab_node() const noexcept {
  return query_prefab_node(selection_);
}
engine::error scene_document::select(scene_node_id node) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  if (node) {
    auto status =
        with_scene([&](auto& scenes) { return scenes.validate_editable_node(scene_, node); });
    if (status.failed())
      return status;
  }
  if (node) {
    entity_id entity;
    auto status = world_->node_get_entity(node, entity);
    if (status.failed())
      return status;
  }
  selection_ = node;
  return engine::error::success;
}
engine::error scene_document::validate_selection() noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  if (!selection_)
    return engine::error::success;
  auto status = select(selection_);
  if (status.failed()) {
    selection_ = {};
    if (status == engine::error::invalid_handle || status == engine::error::not_found ||
        status == engine::error::invalid_argument)
      return engine::error::success;
  }
  return status;
}

engine::result<engine::scene_mesh_binding>
scene_document::capture_mesh_binding(scene_node_id node) const noexcept {
  return with_scene([&](auto& scenes) { return scenes.capture_mesh_binding(scene_, node); });
}

engine::result<scene_node_id>
scene_document::create_mesh_node(const mesh_node_creation& snapshot,
                                 const engine::scene_mesh_binding& resources) noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  if (snapshot.uuid.is_nil() || find_node(snapshot.uuid).ok())
    return engine::error::invalid_argument;
  scene_node_id parent;
  if (!snapshot.parent_uuid.is_nil()) {
    const auto record = find_node(snapshot.parent_uuid);
    if (!record)
      return record.error();
    parent = record->node;
  }
  auto created = with_scene([&](auto& scenes) {
    return scenes.create_mesh_renderer_node_ready(scene_,
                                                  {.uuid = snapshot.uuid,
                                                   .name = snapshot.display_name,
                                                   .parent = parent,
                                                   .mesh_uri = snapshot.mesh_uri,
                                                   .material_uri = snapshot.material_uri},
                                                  resources);
  });
  if (!created)
    return created.error();
  selection_ = *created;
  is_dirty_ = true;
  return *created;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error scene_document::destroy_node(scene_node_id node) noexcept {
  if (!is_open() || !node.is_valid())
    return engine::error::invalid_argument;
  // 删除由场景验证，界面缓存不作为可撤销数据源；完整撤销使用子树快照。
  const auto status = with_scene([&](auto& service) { return service.destroy_node(scene_, node); });
  if (status.failed())
    return status;
  if (selection_ == node)
    selection_ = {};
  is_dirty_ = true;
  return engine::error::success;
}

gneiss::engine::error scene_document::set_mesh_renderer(scene_node_id node,
                                                        std::string_view mesh_uri,
                                                        std::string_view material_uri) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto status = with_scene(
      [&](auto& scenes) { return scenes.set_mesh_renderer(scene_, node, mesh_uri, material_uri); });
  if (status.ok())
    is_dirty_ = true;
  return status;
}

engine::error
scene_document::replace_mesh_binding(scene_node_id node, const engine::scene_mesh_binding& expected,
                                     const engine::scene_mesh_binding& replacement) noexcept {
  const auto status = with_scene([&](auto& scenes) {
    return scenes.replace_mesh_binding(scene_, node, expected, replacement);
  });
  if (status.ok())
    is_dirty_ = true;
  return status;
}

engine::error scene_document::encode_scene(std::string& output) const noexcept try {
  engine::scene_description description;
  auto description_result =
      with_scene([&](auto& service) { return service.capture_description(scene_); });
  if (!description_result)
    return description_result.error();
  description = std::move(*description_result);
  auto encoded = engine::meta::serialize(description);
  if (!encoded)
    return encoded.error().error;
  output.assign(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_document::begin_save(gneiss::engine::files::service& files,
                                         std::string_view uri,
                                         gneiss::engine::files::write_mode mode,
                                         std::shared_ptr<save_receipt>& output) noexcept {
  output.reset();
  if (!is_open() || !uri.starts_with("asset://") || engine::validate_uri(uri).failed())
    return engine::error::invalid_argument;
  try {
    auto receipt = std::make_shared<save_receipt>();
    receipt->scene = scene_;
    receipt->uri = uri;
    auto status = encode_scene(receipt->content);
    if (status.failed())
      return status;
    const auto bytes = std::as_bytes(std::span{receipt->content.data(), receipt->content.size()});
    gneiss::engine::files::request_id request;
    status = files.write(
        uri, {bytes.begin(), bytes.end()}, {mode},
        [receipt](engine::error value) {
          receipt->status = value;
          receipt->complete = true;
        },
        request);
    if (status.ok())
      output = std::move(receipt);
    return status;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error scene_document::accept_save(const save_receipt& receipt) noexcept {
  if (!receipt.complete)
    return engine::error::not_ready;
  if (receipt.status.failed())
    return receipt.status;
  if (!is_open() || scene_ != receipt.scene)
    return engine::error::invalid_state;
  try {
    std::string current;
    const auto status = encode_scene(current);
    if (status.failed())
      return status;
    uri_ = receipt.uri;
    is_dirty_ = current != receipt.content;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

} // namespace gneiss::editor
