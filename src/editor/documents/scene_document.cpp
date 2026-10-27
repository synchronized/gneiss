// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/scene_document.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/result.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/resource_registry.hpp"
#include "runtime/application.hpp"

#include "engine/function/world/component_identity.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <new>
#include <random>
#include <sstream>
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

[[nodiscard]] std::string make_uuid_value() {
  std::array<std::uint8_t, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<std::uint8_t>(random());
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4U || index == 6U || index == 8U || index == 10U) {
      output << '-';
    }
    output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return output.str();
}

} // namespace

gneiss::engine::result make_editor_uuid(std::string& output) noexcept {
  try {
    output = make_uuid_value();
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result make_asset_uri(const std::filesystem::path& asset_root,
                                      const std::filesystem::path& path,
                                      std::string& output) noexcept {
  if (asset_root.empty() || path.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    std::error_code error;
    const auto root = std::filesystem::canonical(asset_root, error);
    if (error) {
      return gneiss::engine::result::io;
    }
    const auto target = std::filesystem::weakly_canonical(path, error);
    if (error || !is_within(root, target)) {
      return gneiss::engine::result::invalid_argument;
    }
    const auto relative = std::filesystem::relative(target, root, error).generic_u8string();
    if (error || relative.empty()) {
      return gneiss::engine::result::invalid_argument;
    }
    std::string uri = "asset://";
    uri.append(reinterpret_cast<const char*>(relative.data()), relative.size());
    if (gneiss::engine::validate_uri(uri) != gneiss::engine::result::success) {
      return gneiss::engine::result::invalid_argument;
    }
    output = std::move(uri);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::io;
  }
}

bool scene_document::is_open() const noexcept {
  if (!application_ || !scene_ || !application_->is_owner_thread() ||
      !application_->accepts_requests() || application_->native_world() != world_)
    return false;
  // 地址可能被后续 World 复用；实例 ID 的所属域仍必须匹配当前实例集合。
  auto* scenes = application_->scenes();
  std::uint64_t count{};
  return scenes && scenes->get_node_count(scene_, &count).ok();
}
template <class F> gneiss::engine::result scene_document::with_scene(F&& action) const noexcept {
  if (!application_ || !application_->is_owner_thread() || !is_open())
    return gneiss::engine::result::invalid_state;
  auto* service = application_->scenes();
  return service ? action(*service) : gneiss::engine::result::invalid_state;
}
template <class F>
gneiss::engine::result scene_document::with_node(scene_node_id& output, F&& action) const noexcept {
  scene_node_id node{};
  const auto operation = with_scene([&](auto& service) { return action(service, node); });
  output = scene_node_id{node};
  return operation;
}

scene_document::scene_document(scene_document&& other) noexcept { *this = std::move(other); }
scene_document& scene_document::operator=(scene_document&& other) noexcept {
  if (this != &other) {
    close();
    application_ = std::exchange(other.application_, nullptr);
    world_ = std::exchange(other.world_, nullptr);
    scene_ = std::exchange(other.scene_, scene_instance_id{});
    nodes_ = std::move(other.nodes_);
    prefab_nodes_ = std::move(other.prefab_nodes_);
    selection_ = std::exchange(other.selection_, {});
    uri_ = std::move(other.uri_);
    is_dirty_ = std::exchange(other.is_dirty_, false);
  }
  return *this;
}

gneiss::engine::result scene_document::adopt(runtime::application& application,
                                             gneiss::engine::world* world,
                                             gneiss::scene_instance_id scene,
                                             std::string_view uri) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (world != application.native_world() || uri.empty() || !scene)
    return gneiss::engine::result::invalid_argument;
  try {
    scene_document pending;
    pending.uri_ = uri;
    pending.application_ = &application;
    pending.world_ = world;
    pending.scene_ = scene;
    const auto refreshed = pending.refresh_nodes();
    if (refreshed != gneiss::engine::result::success) {
      // 失败时仍由调用方持有场景，不改变当前文档。
      pending.scene_ = {};
      return refreshed;
    }
    *this = std::move(pending);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::create_empty(runtime::application& application,
                                                    gneiss::engine::world* world,
                                                    std::string_view scene_uuid) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (world != application.native_world() || scene_uuid.empty())
    return gneiss::engine::result::invalid_argument;
  scene_instance_id pending{};
  const auto created = application.scenes()->create_empty(scene_uuid, &pending);
  if (created != gneiss::engine::result::success)
    return created;
  close();
  application_ = &application;
  scene_ = pending;
  world_ = world;
  is_dirty_ = true;
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_document::create_empty(runtime::application& application,
                                                    gneiss::engine::world* world) noexcept {
  try {
    return create_empty(application, world, make_uuid_value());
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::synchronize_nodes() noexcept {
  const auto previous_selection = selection_;
  selection_ = {};
  const auto status = refresh_nodes();
  if (status.failed()) {
    nodes_.clear();
    prefab_nodes_.clear();
    selection_ = {};
  } else if (previous_selection) {
    // 删除或替换导致选择失效是正常结果，不是节点列表同步失败。
    (void)select(previous_selection);
  }
  return status;
}

gneiss::engine::result scene_document::refresh_nodes() noexcept {
  try {
    std::uint64_t count = 0;
    auto operation =
        with_scene([&](auto& service) { return service.get_node_count(scene_, &count); });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    std::vector<scene_node_record> records;
    records.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      gneiss::engine::scene_node_view info{};
      operation =
          with_scene([&](auto& service) { return service.get_node_info(scene_, index, info); });
      if (operation != gneiss::engine::result::success) {
        return operation;
      }
      std::string uuid{info.uuid};
      records.push_back(
          {.node = scene_node_id{info.node},
           .parent = scene_node_id{info.parent},
           .entity = entity_id{info.entity},
           .uuid = uuid,
           .display_name = info.name.empty() ? std::move(uuid) : std::string{info.name},
           .mesh_uri = info.mesh_uri.empty() ? std::string{} : std::string{info.mesh_uri},
           .material_uri =
               info.material_uri.empty() ? std::string{} : std::string{info.material_uri},
           .local_transform = {info.local_transform.translation, info.local_transform.rotation,
                               info.local_transform.scale},
           .component_flags = static_cast<std::uint32_t>(info.component_flags),
           .camera = info.camera.value_or(gneiss::engine::camera_description{}),
           .is_primary_camera =
               has_flags(info.component_flags, scene_node_components::primary_camera)});
    }
    std::uint64_t prefab_count = 0;
    operation = with_scene(
        [&](auto& service) { return service.get_prefab_node_count(scene_, &prefab_count); });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    std::vector<prefab_node_record> prefab_records;
    prefab_records.reserve(static_cast<std::size_t>(prefab_count));
    for (std::uint64_t index = 0; index < prefab_count; ++index) {
      gneiss::engine::prefab_node_view info{};
      operation = with_scene(
          [&](auto& service) { return service.get_prefab_node_info(scene_, index, info); });
      if (operation != gneiss::engine::result::success) {
        return operation;
      }
      const std::string instance_uuid{info.instance_uuid};
      const std::string source_uuid =
          info.source_node_uuid.empty() ? std::string{} : std::string{info.source_node_uuid};
      const bool is_root = has_flags(info.flags, scene_prefab_flags::instance_root);
      prefab_records.push_back(
          {.node = scene_node_id{info.node},
           .parent = scene_node_id{info.parent},
           .entity = entity_id{info.entity},
           .instance_uuid = instance_uuid,
           .source_node_uuid = source_uuid,
           .display_name =
               info.name.empty() ? (is_root ? instance_uuid : source_uuid) : std::string{info.name},
           .prefab_uri = std::string{info.prefab_uri},
           .local_transform = {info.local_transform.translation, info.local_transform.rotation,
                               info.local_transform.scale},
           .source_local_transform = {info.source_local_transform.translation,
                                      info.source_local_transform.rotation,
                                      info.source_local_transform.scale},
           .override_flags =
               static_cast<std::uint32_t>(info.flags) &
               (static_cast<std::uint32_t>(scene_prefab_flags::translation_overridden) |
                static_cast<std::uint32_t>(scene_prefab_flags::rotation_overridden) |
                static_cast<std::uint32_t>(scene_prefab_flags::scale_overridden)),
           .is_instance_root = is_root,
           .is_read_only = has_flags(info.flags, scene_prefab_flags::source_read_only)});
    }
    nodes_.swap(records);
    prefab_nodes_.swap(prefab_records);
    return validate_selection();
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_document::get_world_transform(scene_node_id node,
                                    gneiss::engine::math::transform& output) const noexcept {
  gneiss::engine::math::transform value;
  const auto operation = with_scene(
      [&](auto&) { return application_->native_world()->node_get_world_transform(node, value); });
  if (operation.ok())
    output = value;
  return operation;
}

gneiss::engine::result
scene_document::set_local_transform(scene_node_id node,
                                    const gneiss::engine::math::transform& value) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  auto prefab = std::ranges::find(prefab_nodes_, node, &prefab_node_record::node);
  if (found == nodes_.end() && prefab == prefab_nodes_.end()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto operation = prefab != prefab_nodes_.end() && !prefab->is_instance_root
                             ? with_scene([&](auto& service) {
                                 return service.set_prefab_source_transform(
                                     scene_, gneiss::scene_node_id{node},
                                     {value.translation, value.rotation, value.scale});
                               })
                             : application_->native_world()->node_set_local_transform(
                                   node, {value.translation, value.rotation, value.scale});
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  if (prefab != prefab_nodes_.end() && !prefab->is_instance_root) {
    prefab->local_transform = value;
    prefab->override_flags = 0U;
    if (!(value.translation == prefab->source_local_transform.translation)) {
      prefab->override_flags |=
          static_cast<std::uint32_t>(scene_prefab_flags::translation_overridden);
    }
    if (!(value.rotation == prefab->source_local_transform.rotation)) {
      prefab->override_flags |= static_cast<std::uint32_t>(scene_prefab_flags::rotation_overridden);
    }
    if (!(value.scale == prefab->source_local_transform.scale)) {
      prefab->override_flags |= static_cast<std::uint32_t>(scene_prefab_flags::scale_overridden);
    }
    is_dirty_ = true;
    return gneiss::engine::result::success;
  }
  if (found != nodes_.end()) {
    found->local_transform = value;
  } else {
    prefab->local_transform = value;
  }
  is_dirty_ = true;
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_document::restore_prefab_transform_field(
    scene_node_id node, std::uint32_t field_id,
    gneiss::engine::math::transform& out_previous) noexcept {
  const auto found = std::ranges::find(prefab_nodes_, node, &prefab_node_record::node);
  if (found == prefab_nodes_.end() || found->is_instance_root) {
    return gneiss::engine::result::invalid_argument;
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
    return gneiss::engine::result::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, restored);
  if (operation == gneiss::engine::result::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::engine::result
scene_document::restore_prefab_transform(scene_node_id node,
                                         gneiss::engine::math::transform& out_previous) noexcept {
  const auto found = std::ranges::find(prefab_nodes_, node, &prefab_node_record::node);
  if (found == prefab_nodes_.end() || found->is_instance_root) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, found->source_local_transform);
  if (operation == gneiss::engine::result::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::engine::result scene_document::create_node(std::string_view name, scene_node_id parent,
                                                   scene_node_id& out_node) noexcept {
  if (!is_open()) {
    return gneiss::engine::result::invalid_state;
  }
  try {
    const auto uuid = make_uuid_value();
    gneiss::engine::node_creation desc{};
    desc.uuid = uuid;
    desc.name = name;
    desc.parent = parent;
    auto operation = with_node(out_node, [&](auto& service, auto& output) {
      return service.create_node(scene_, desc, output);
    });
    if (operation == gneiss::engine::result::success) {
      selection_ = out_node;
      operation = refresh_nodes();
    }
    if (operation == gneiss::engine::result::success) {
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::rename_prefab_instance(scene_node_id root,
                                                              std::string_view name) noexcept {
  const auto operation = with_scene([&](auto& service) {
    return service.set_prefab_instance_name(scene_, gneiss::scene_node_id{root}, name);
  });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

gneiss::engine::result scene_document::rename_node(scene_node_id node,
                                                   std::string_view name) noexcept {
  if (application_ && !application_->is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto operation = with_scene([&](auto& service) {
    return service.set_node_name(scene_, gneiss::scene_node_id{node}, name);
  });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

gneiss::engine::result scene_document::reparent_node(scene_node_id node,
                                                     scene_node_id parent) noexcept {
  if (!is_open() || !node.is_valid() || node == parent) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto operation = with_scene([&](auto& service) {
    return service.reparent_node(scene_, gneiss::scene_node_id{node},
                                 gneiss::scene_node_id{parent});
  });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

engine::result
scene_document::capture_subtree_bindings(scene_node_id root,
                                         std::vector<engine::scene_mesh_binding>& output) const {
  for (const auto& candidate : nodes_) {
    if (candidate.mesh_uri.empty())
      continue;
    auto current = candidate.node;
    while (current.is_valid()) {
      if (current == root) {
        engine::scene_mesh_binding binding;
        const auto status = application_->capture_mesh_binding(scene_, candidate.node, binding);
        if (status.failed())
          return status;
        output.push_back(std::move(binding));
        break;
      }
      const auto ancestor = std::ranges::find(nodes_, current, &scene_node_record::node);
      current = ancestor == nodes_.end() ? scene_node_id{} : ancestor->parent;
    }
  }
  return engine::result::success;
}

gneiss::engine::result
scene_document::destroy_subtree(scene_node_id node, scene_subtree_snapshot& out_snapshot) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return gneiss::engine::result::not_found;
  }
  try {
    scene_subtree_snapshot snapshot{
        .json = {}, .parent_uuid = {}, .root_uuid = found->uuid, .resources = {}};
    if (found->parent.is_valid()) {
      const auto parent = std::ranges::find(nodes_, found->parent, &scene_node_record::node);
      if (parent == nodes_.end()) {
        return gneiss::engine::result::invalid_state;
      }
      snapshot.parent_uuid = parent->uuid;
    }
    auto operation = with_scene([&](auto& service) {
      return service.capture_subtree(scene_, gneiss::scene_node_id{node}, snapshot.json);
    });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    operation = capture_subtree_bindings(node, snapshot.resources);
    if (operation.failed())
      return operation;
    operation = with_scene([&](auto& service) {
      return service.destroy_subtree(scene_, gneiss::scene_node_id{node});
    });
    if (operation == gneiss::engine::result::success) {
      selection_ = {};
      operation = refresh_nodes();
    }
    if (operation == gneiss::engine::result::success) {
      out_snapshot = std::move(snapshot);
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::restore_subtree(const scene_subtree_snapshot& snapshot,
                                                       scene_node_id& out_node) noexcept {
  scene_node_id parent;
  if (!snapshot.parent_uuid.empty()) {
    const auto* record = find_node(snapshot.parent_uuid);
    if (record == nullptr) {
      return gneiss::engine::result::not_found;
    }
    parent = record->node;
  }
  const auto operation = with_node(out_node, [&](auto& service, auto& output) {
    return service.restore_subtree(scene_, snapshot.json, gneiss::scene_node_id{parent}, {}, output,
                                   snapshot.resources);
  });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  selection_ = out_node;
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

gneiss::engine::result scene_document::duplicate_subtree(scene_node_id node, scene_node_id parent,
                                                         scene_node_id& out_node) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    std::string snapshot;
    auto operation = with_scene([&](auto& service) {
      return service.capture_subtree(scene_, gneiss::scene_node_id{node}, snapshot);
    });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    std::vector<engine::scene_mesh_binding> bindings;
    operation = capture_subtree_bindings(node, bindings);
    if (operation.failed())
      return operation;
    std::vector<std::string> sources;
    std::vector<std::string> targets;
    for (const auto& candidate : nodes_) {
      auto current = candidate.node;
      while (current.is_valid()) {
        if (current == node) {
          sources.push_back(candidate.uuid);
          targets.push_back(make_uuid_value());
          break;
        }
        const auto ancestor = std::ranges::find(nodes_, current, &scene_node_record::node);
        current = ancestor == nodes_.end() ? scene_node_id{} : ancestor->parent;
      }
    }
    std::vector<gneiss::engine::uuid_mapping> mappings;
    mappings.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
      mappings.push_back({.source_uuid = sources[index], .target_uuid = targets[index]});
    }
    operation = with_node(out_node, [&](auto& service, auto& output) {
      return service.restore_subtree(scene_, snapshot, gneiss::scene_node_id{parent}, mappings,
                                     output, bindings);
    });
    if (operation == gneiss::engine::result::success) {
      selection_ = out_node;
      operation = refresh_nodes();
    }
    if (operation == gneiss::engine::result::success) {
      is_dirty_ = true;
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_document::set_camera(scene_node_id node,
                           const gneiss::engine::camera_description& desc) noexcept {
  const auto operation = with_scene(
      [&](auto& service) { return service.set_camera(scene_, gneiss::scene_node_id{node}, desc); });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

gneiss::engine::result scene_document::remove_camera(scene_node_id node) noexcept {
  const auto operation = with_scene(
      [&](auto& service) { return service.remove_camera(scene_, gneiss::scene_node_id{node}); });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

gneiss::engine::result scene_document::remove_mesh_renderer(scene_node_id node) noexcept {
  const auto operation = with_scene([&](auto& service) {
    return service.remove_mesh_renderer(scene_, gneiss::scene_node_id{node});
  });
  if (operation != gneiss::engine::result::success) {
    return operation;
  }
  const auto refreshed = refresh_nodes();
  if (refreshed == gneiss::engine::result::success) {
    is_dirty_ = true;
  }
  return refreshed;
}

void scene_document::close() noexcept {
  selection_ = {};
  nodes_.clear();
  prefab_nodes_.clear();
  uri_.clear();
  is_dirty_ = false;
  if (is_open())
    (void)with_scene([&](auto& service) { return service.unload(scene_); });
  scene_ = {};
  application_ = nullptr;
  world_ = 0;
}

const scene_node_record* scene_document::selected_node() const noexcept {
  const auto found = std::ranges::find(nodes_, selection_, &scene_node_record::node);
  return found == nodes_.end() ? nullptr : &*found;
}

const prefab_node_record* scene_document::selected_prefab_node() const noexcept {
  const auto found = std::ranges::find(prefab_nodes_, selection_, &prefab_node_record::node);
  return found == prefab_nodes_.end() ? nullptr : &*found;
}

const prefab_node_record*
scene_document::find_prefab_root(std::string_view instance_uuid) const noexcept {
  const auto found = std::ranges::find_if(prefab_nodes_, [instance_uuid](const auto& node) {
    return node.is_instance_root && node.instance_uuid == instance_uuid;
  });
  return found == prefab_nodes_.end() ? nullptr : &*found;
}

const prefab_node_record*
scene_document::find_prefab_source(std::string_view instance_uuid,
                                   std::string_view source_uuid) const noexcept {
  const auto found = std::ranges::find_if(prefab_nodes_, [&](const auto& node) {
    return !node.is_instance_root && node.instance_uuid == instance_uuid &&
           node.source_node_uuid == source_uuid;
  });
  return found == prefab_nodes_.end() ? nullptr : &*found;
}

const scene_node_record* scene_document::find_node(std::string_view uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, uuid, &scene_node_record::uuid);
  return found == nodes_.end() ? nullptr : &*found;
}

gneiss::engine::result scene_document::select(scene_node_id node) noexcept {
  if (!is_open() || !application_->is_owner_thread())
    return gneiss::engine::result::invalid_state;
  if (!node.is_valid()) {
    selection_ = {};
    return gneiss::engine::result::success;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  const auto prefab = std::ranges::find(prefab_nodes_, node, &prefab_node_record::node);
  if (found == nodes_.end() && prefab == prefab_nodes_.end()) {
    return gneiss::engine::result::not_found;
  }
  entity_id entity{};
  const auto operation = application_->native_world()->node_get_entity(node, entity);
  const auto expected = found != nodes_.end() ? found->entity : prefab->entity;
  if (operation != gneiss::engine::result::success || entity != expected) {
    selection_ = {};
    return operation == gneiss::engine::result::success ? gneiss::engine::result::invalid_handle
                                                        : operation;
  }
  selection_ = node;
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_document::validate_selection() noexcept {
  return selection_.is_valid() ? select(selection_) : gneiss::engine::result::success;
}

gneiss::engine::result scene_document::create_mesh_renderer_node(std::string_view name,
                                                                 std::string_view mesh_uri,
                                                                 std::string_view material_uri,
                                                                 scene_node_id& out_node) noexcept {
  if (!is_open() || mesh_uri.empty() || material_uri.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    const auto uuid = make_uuid_value();
    return create_mesh_renderer_node({.uuid = uuid,
                                      .parent_uuid = {},
                                      .display_name = name.empty() ? uuid : std::string{name},
                                      .mesh_uri = std::string{mesh_uri},
                                      .material_uri = std::string{material_uri}},
                                     out_node);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_document::create_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                          scene_node_id& out_node) noexcept {
  if (!is_open() || snapshot.uuid.empty() || snapshot.mesh_uri.empty() ||
      snapshot.material_uri.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    scene_node_id parent;
    if (!snapshot.parent_uuid.empty()) {
      const auto* parent_record = find_node(snapshot.parent_uuid);
      if (parent_record == nullptr) {
        return gneiss::engine::result::not_found;
      }
      parent = parent_record->node;
    }
    nodes_.reserve(nodes_.size() + 1U);
    gneiss::engine::mesh_renderer_node_creation desc{};
    desc.uuid = snapshot.uuid;
    desc.name = snapshot.display_name.empty() ? std::string_view{}
                                              : std::string_view{snapshot.display_name};
    desc.parent = parent;
    desc.mesh_uri = snapshot.mesh_uri;
    desc.material_uri = snapshot.material_uri;
    auto operation = with_node(out_node, [&](auto& service, auto& output) {
      return service.create_mesh_renderer_node(scene_, desc, output);
    });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    std::uint64_t count = 0;
    gneiss::engine::scene_node_view info{};
    operation = with_scene([&](auto& service) { return service.get_node_count(scene_, &count); });
    if (operation == gneiss::engine::result::success && count > 0U) {
      operation = with_scene(
          [&](auto& service) { return service.get_node_info(scene_, count - 1U, info); });
    }
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    nodes_.push_back(
        {.node = scene_node_id{info.node},
         .parent = scene_node_id{info.parent},
         .entity = entity_id{info.entity},
         .uuid = snapshot.uuid,
         .display_name = snapshot.display_name.empty() ? snapshot.uuid : snapshot.display_name,
         .mesh_uri = snapshot.mesh_uri,
         .material_uri = snapshot.material_uri});
    selection_ = out_node;
    is_dirty_ = true;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::commit_mesh_node(
    const scene_node_snapshot& snapshot,
    const std::function<gneiss::engine::result(scene_node_id, scene_node_id&)>& create,
    scene_node_id& output) noexcept try {
  output = {};
  if (!is_open() || !create || snapshot.uuid.empty() || find_node(snapshot.uuid) != nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  scene_node_id parent;
  if (!snapshot.parent_uuid.empty()) {
    const auto* record = find_node(snapshot.parent_uuid);
    if (record == nullptr) {
      return gneiss::engine::result::not_found;
    }
    parent = record->node;
  }
  scene_node_record record{
      .node = {},
      .parent = parent,
      .entity = {},
      .uuid = snapshot.uuid,
      .display_name = snapshot.display_name.empty() ? snapshot.uuid : snapshot.display_name,
      .mesh_uri = snapshot.mesh_uri,
      .material_uri = snapshot.material_uri,
  };
  nodes_.reserve(nodes_.size() + 1U);
  auto operation = create(parent, output);
  if (operation.failed()) {
    return operation;
  }
  entity_id entity{};
  operation = application_->native_world()->node_get_entity(output, entity);
  if (operation.failed()) {
    (void)with_scene(
        [&](auto& service) { return service.destroy_node(scene_, gneiss::scene_node_id{output}); });
    output = {};
    return operation;
  }
  record.node = output;
  record.entity = entity_id{entity};
  nodes_.push_back(std::move(record));
  selection_ = output;
  is_dirty_ = true;
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result
scene_document::restore_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                           scene_node_id& out_node) noexcept {
  return create_mesh_renderer_node(snapshot, out_node);
}

gneiss::engine::result scene_document::destroy_node(scene_node_id node,
                                                    scene_node_snapshot& out_snapshot) noexcept {
  if (!is_open() || !node.is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return gneiss::engine::result::not_found;
  }
  if (found->mesh_uri.empty() || found->material_uri.empty()) {
    return gneiss::engine::result::unsupported;
  }
  if (std::ranges::any_of(nodes_,
                          [node](const auto& candidate) { return candidate.parent == node; })) {
    return gneiss::engine::result::invalid_state;
  }
  try {
    scene_node_snapshot snapshot{.uuid = found->uuid,
                                 .parent_uuid = {},
                                 .display_name = found->display_name,
                                 .mesh_uri = found->mesh_uri,
                                 .material_uri = found->material_uri};
    if (found->parent.is_valid()) {
      const auto parent = std::ranges::find(nodes_, found->parent, &scene_node_record::node);
      if (parent == nodes_.end()) {
        return gneiss::engine::result::invalid_state;
      }
      snapshot.parent_uuid = parent->uuid;
    }
    const auto operation = with_scene(
        [&](auto& service) { return service.destroy_node(scene_, gneiss::scene_node_id{node}); });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    nodes_.erase(found);
    if (selection_ == node) {
      selection_ = {};
    }
    out_snapshot = std::move(snapshot);
    is_dirty_ = true;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::set_mesh_renderer(scene_node_id node,
                                                         std::string_view mesh_uri,
                                                         std::string_view material_uri) noexcept {
  if (!is_open() || !node.is_valid() || mesh_uri.empty() || material_uri.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return gneiss::engine::result::not_found;
  }
  try {
    std::string mesh(mesh_uri);
    std::string material(material_uri);

    const auto operation = with_scene(
        [&](auto& service) { return service.set_mesh_renderer(scene_, node, mesh, material); });
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    found->mesh_uri.swap(mesh);
    found->material_uri.swap(material);
    is_dirty_ = true;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_document::commit_mesh_renderer(
    scene_node_id node, std::string_view mesh_uri, std::string_view material_uri,
    const std::function<gneiss::engine::result()>& commit) noexcept try {
  if (!is_open() || !commit || mesh_uri.empty() || material_uri.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto found = std::ranges::find(nodes_, node, &scene_node_record::node);
  if (found == nodes_.end()) {
    return gneiss::engine::result::not_found;
  }
  std::string mesh{mesh_uri};
  std::string material{material_uri};
  const auto operation = commit();
  if (operation.failed()) {
    return operation;
  }
  found->mesh_uri.swap(mesh);
  found->material_uri.swap(material);
  found->component_flags |= static_cast<std::uint32_t>(scene_node_components::mesh_renderer);
  is_dirty_ = true;
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

engine::result scene_document::encode_scene(std::string& output) const noexcept try {
  engine::scene_description description;
  auto status =
      with_scene([&](auto& service) { return service.capture_description(scene_, description); });
  if (status.failed())
    return status;
  engine::meta::registry_ref registry;
  status = engine::builtin_resource_registry(registry);
  if (status.failed())
    return status;
  gneiss::engine::files::bytes encoded;
  engine::meta::serialization_diagnostic diagnostic;
  status = engine::meta::serialize(registry, description, encoded, diagnostic);
  if (status.ok())
    output.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
  return status;
} catch (const std::bad_alloc&) {
  return engine::result::out_of_memory;
} catch (...) {
  return engine::result::internal;
}

engine::result scene_document::begin_save(gneiss::engine::files::service& files,
                                          std::string_view uri,
                                          gneiss::engine::files::write_mode mode,
                                          std::shared_ptr<save_receipt>& output) noexcept {
  output.reset();
  if (!is_open() || !uri.starts_with("asset://") || engine::validate_uri(uri).failed())
    return engine::result::invalid_argument;
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
        [receipt](engine::result value) {
          receipt->status = value;
          receipt->complete = true;
        },
        request);
    if (status.ok())
      output = std::move(receipt);
    return status;
  } catch (const std::bad_alloc&) {
    return engine::result::out_of_memory;
  } catch (...) {
    return engine::result::internal;
  }
}
engine::result scene_document::accept_save(const save_receipt& receipt) noexcept {
  if (!receipt.complete)
    return engine::result::not_ready;
  if (receipt.status.failed())
    return receipt.status;
  if (!is_open() || scene_ != receipt.scene)
    return engine::result::invalid_state;
  try {
    std::string current;
    const auto status = encode_scene(current);
    if (status.failed())
      return status;
    uri_ = receipt.uri;
    is_dirty_ = current != receipt.content;
    return engine::result::success;
  } catch (const std::bad_alloc&) {
    return engine::result::out_of_memory;
  } catch (...) {
    return engine::result::internal;
  }
}

} // namespace gneiss::editor
