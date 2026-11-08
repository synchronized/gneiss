// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/scene_edit_session.hpp"
#include "documents/scene_projection.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime/application.hpp"

#include "engine/function/world/component_identity.hpp"

#include "engine/resource/asset_uri.hpp"

#include "engine/function/scene/prefab_replay.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <random>
#include <set>
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

[[nodiscard]] gneiss::engine::guid make_uuid_value() { return engine::guid::generate(); }

engine::scene_mesh_binding binding_for(const engine::component_descriptions& components,
                                       const scene_document_snapshot& snapshot) {
  engine::scene_mesh_binding result;
  const auto* renderer = components.get<engine::mesh_renderer_description>();
  if (!renderer)
    return result;
  if (auto found = snapshot.assets.find(renderer->mesh_uri); found != snapshot.assets.end())
    result.mesh = engine::resource_cast<engine::mesh>(found->second);
  if (auto found = snapshot.assets.find(renderer->material_uri); found != snapshot.assets.end())
    result.material = engine::resource_cast<engine::material>(found->second);
  return result;
}
void retain(scene_document_snapshot& snapshot, const engine::scene_mesh_binding& binding) {
  if (binding.mesh)
    snapshot.assets.insert_or_assign(binding.mesh->source().uri, binding.mesh);
  if (binding.material)
    snapshot.assets.insert_or_assign(binding.material->source().uri, binding.material);
}
engine::error import_subtree(const engine::scene_subtree_snapshot& subtree,
                             engine::scene_description& description,
                             scene_document_snapshot& snapshot, bool replacing) {
  auto incoming = subtree.description();
  if (!incoming)
    return incoming.error();
  for (auto object : incoming->objects) {
    if (object.prefab && !subtree.resources.contains(object.uuid))
      return engine::error::not_found;
    if (object.uuid == subtree.root_uuid)
      object.parent_uuid = subtree.parent_uuid;
    auto existing =
        std::ranges::find(description.objects, object.uuid, &engine::game_object_description::uuid);
    if (existing != description.objects.end()) {
      if (!replacing)
        return engine::error::invalid_argument;
      *existing = object;
    } else
      description.objects.push_back(object);
    if (auto resources = subtree.resources.find(object.uuid);
        resources != subtree.resources.end()) {
      retain(snapshot, resources->second.renderer);
      for (const auto& binding : resources->second.source_nodes)
        retain(snapshot, binding);
      if (object.prefab)
        snapshot.prefabs.insert_or_assign(object.prefab->uri,
                                          prefab_document_snapshot{resources->second.prefab_content,
                                                                   resources->second.source_asset});
    }
  }
  return engine::error::success;
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

bool scene_edit_session::is_open() const noexcept {
  if (!application_ || !scene_ || !application_->is_owner_thread() ||
      !application_->accepts_requests() || application_->native_world() != world_)
    return false;
  // 地址可能被后续 World 复用；实例 ID 的所属域仍必须匹配当前实例集合。
  auto* scenes = application_->scenes();
  return scenes && document_ && bool(document_->current());
}
template <class F>
std::invoke_result_t<F, engine::scene_instances&>
scene_edit_session::with_scene(F&& action) const noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  auto* service = application_->scenes();
  if (!service)
    return engine::error::invalid_state;
  return std::forward<F>(action)(*service);
}

scene_edit_session::scene_edit_session(scene_edit_session&& other) noexcept {
  *this = std::move(other);
}
scene_edit_session& scene_edit_session::operator=(scene_edit_session&& other) noexcept {
  if (this != &other) {
    close();
    document_ = std::move(other.document_);
    projection_ = std::move(other.projection_);
    application_ = std::exchange(other.application_, nullptr);
    world_ = std::exchange(other.world_, nullptr);
    scene_ = std::exchange(other.scene_, scene_instance_id{});
    selection_ = std::exchange(other.selection_, {});
    uri_ = std::move(other.uri_);
  }
  return *this;
}

gneiss::engine::error
scene_edit_session::adopt(runtime::application& application, gneiss::engine::world* world,
                          gneiss::scene_instance_id scene, const scene_document_snapshot& initial,
                          std::span<const engine::scene_object_binding> objects) noexcept {
  if (edit_base_ || !application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (world != application.native_world() || initial.uri.empty() || !scene)
    return gneiss::engine::error::invalid_argument;
  try {
    const auto count = application.scenes()->get_node_count(scene);
    if (!count)
      return count.error();
    if (*count != objects.size())
      return engine::error::invalid_argument;
    for (const auto& binding : objects)
      if (!world->object(binding.entity))
        return engine::error::invalid_handle;
    scene_edit_session pending;
    pending.uri_ = initial.uri;
    pending.application_ = &application;
    pending.world_ = world;
    pending.scene_ = scene;
    pending.document_ = std::make_unique<scene_document>();
    auto refreshed = pending.projection_.reset(objects);
    if (refreshed.ok())
      refreshed = pending.document_->reset(initial);
    if (refreshed.ok())
      refreshed = pending.report_projection(pending.document_->current(), engine::error::success);
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

gneiss::engine::error scene_edit_session::create_empty(runtime::application& application,
                                                       gneiss::engine::world* world,
                                                       gneiss::engine::guid scene_uuid) noexcept {
  if (edit_base_ || !application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (world != application.native_world() || scene_uuid.is_nil())
    return gneiss::engine::error::invalid_argument;
  auto created = application.scenes()->create_empty(scene_uuid);
  if (!created)
    return created.error();
  engine::scene_instantiation_data initial;
  initial.description.uuid = scene_uuid;
  auto frozen = make_scene_document_snapshot({}, initial);
  if (!frozen) {
    (void)application.scenes()->unload(*created);
    return frozen.error();
  }
  std::unique_ptr<scene_document> versions;
  try {
    versions = std::make_unique<scene_document>();
  } catch (...) {
    (void)application.scenes()->unload(*created);
    return engine::error::out_of_memory;
  }
  const auto status = versions->reset(*frozen, false);
  if (status.failed()) {
    (void)application.scenes()->unload(*created);
    return status;
  }
  close();
  document_ = std::move(versions);
  application_ = &application;
  scene_ = *created;
  world_ = world;
  const auto projected = report_projection(document_->current(), engine::error::success);
  if (projected.failed()) {
    close();
    return projected;
  }
  return gneiss::engine::error::success;
}

gneiss::engine::error scene_edit_session::create_empty(runtime::application& application,
                                                       gneiss::engine::world* world) noexcept {
  try {
    return create_empty(application, world, make_uuid_value());
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

engine::result<scene_document_snapshot> scene_edit_session::capture_content() const noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  auto snapshot = document_->current().content();
  snapshot.uri = uri_;
  return snapshot;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_edit_session::projection_status() const noexcept {
  return document_ ? projection_.status(document_->current())
                   : engine::error{engine::error::invalid_state};
}
engine::error scene_edit_session::accept_content(const scene_document_snapshot& snapshot) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  if (snapshot.uri != uri_)
    return engine::error::invalid_argument;
  const auto before = document_->current();
  const bool synchronized = projection_.is_synchronized(before);
  const auto selected = projection_.address(selection_);
  auto status = document_->accept(before, snapshot);
  if (status.failed())
    return status;
  status = project_scene_document(*world_, *application_->scenes(), scene_, projection_,
                                  synchronized ? &before.content() : nullptr, snapshot);
  (void)report_projection(document_->current(), status);
  if (selected) {
    auto found = projection_.objects().find(*selected);
    selection_ = found == projection_.objects().end() ? scene_node_id{} : found->second.node;
  }
  return engine::error::success;
}
engine::error scene_edit_session::synchronize() noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  const auto version = document_->current();
  auto selected = locate(selection_);
  auto status = project_scene_document(*world_, *application_->scenes(), scene_, projection_,
                                       nullptr, version.content());
  if (status.ok()) {
    selection_ = {};
    if (selected) {
      if (selected->source) {
        auto node = find_prefab_source(selected->object, selected->source);
        if (node)
          selection_ = node->node;
      } else {
        auto node = find_node(selected->object);
        if (node)
          selection_ = node->node;
      }
    }
  }
  return report_projection(version, status);
} catch (const std::bad_alloc&) {
  if (document_)
    (void)report_projection(document_->current(), engine::error::out_of_memory);
  return engine::error::out_of_memory;
} catch (...) {
  if (document_)
    (void)report_projection(document_->current(), engine::error::internal);
  return engine::error::internal;
}
engine::error scene_edit_session::edit_content(
    const content_edit& edit, const std::function<engine::error()>& after_projection) noexcept try {
  auto next = capture_content();
  if (!next)
    return next.error();
  auto description = next->description();
  if (!description)
    return description.error();
  auto status = edit(*description, *next);
  if (status.failed())
    return status;
  auto content = engine::capture_scene_record(*description);
  if (!content)
    return content.error();
  next->content = std::move(*content);
  auto prepared = prepare_scene_document(*next, {});
  if (!prepared)
    return prepared.error();
  for (const auto& dependency : prepared->missing) {
    auto load = application_->assets().load(dependency);
    if (load)
      prepared->snapshot.assets.insert_or_assign(dependency.uri, load->resource());
  }
  const auto before = document_->current();
  const bool synchronized = projection_.is_synchronized(before);
  auto selected =
      selection_ ? locate(selection_) : engine::result<edit_target>{engine::error::not_found};
  status = document_->accept(before, prepared->snapshot);
  if (status.failed())
    return status;
  auto projection = project_scene_document(*world_, *application_->scenes(), scene_, projection_,
                                           synchronized ? &before.content() : nullptr,
                                           document_->current().content(), false);
  if (projection.ok()) {
    if (selected) {
      if (selected->source) {
        auto found = find_prefab_source(selected->object, selected->source);
        selection_ = found ? found->node : scene_node_id{};
      } else {
        auto found = find_node(selected->object);
        selection_ = found ? found->node : scene_node_id{};
      }
    }
    if (after_projection) {
      try {
        projection = after_projection();
      } catch (const std::bad_alloc&) {
        projection = engine::error::out_of_memory;
      } catch (...) {
        projection = engine::error::internal;
      }
    }
  }
  (void)report_projection(document_->current(), projection);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_edit_session::edit_target>
scene_edit_session::locate(scene_node_id node) const noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  if (!node)
    return engine::error::invalid_handle;
  auto identity = projection_.address(node);
  entity_id entity;
  if (!identity || world_->node_get_entity(node, entity).failed() ||
      entity != projection_.objects().at(*identity).entity)
    return engine::error::not_found;
  auto composed = compose_scene_document(document_->current().content());
  if (!composed)
    return composed.error();
  const auto address = *identity;
  auto found = std::ranges::find(composed->objects, address, &engine::composed_object::address);
  if (found == composed->objects.end())
    return engine::error::not_found;
  return edit_target{address.object, address.source, std::move(found->effective)};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_edit_session::report_projection(const version& projected,
                                                    engine::error status) noexcept {
  if (!document_ || document_->current() != projected)
    return engine::error::invalid_state;

  (void)projection_.report(document_->current(), projected, status);
  return status;
}
engine::result<scene_query_snapshot> scene_edit_session::capture_nodes() const noexcept {
  try {
    auto snapshot = capture_content();
    if (!snapshot)
      return snapshot.error();
    auto composed = compose_scene_document(*snapshot);
    if (!composed)
      return composed.error();
    const auto& projected = projection_.objects();
    scene_query_snapshot result;
    for (auto& object : composed->objects) {
      auto found = projected.find(object.address);
      auto parent = projected.find(object.parent);
      const auto node = found == projected.end() ? scene_node_id{} : found->second.node;
      const auto parent_node = parent == projected.end() ? scene_node_id{} : parent->second.node;
      const auto entity = found == projected.end() ? entity_id{} : found->second.entity;
      auto& effective = object.effective;
      const auto display_uuid = object.instance_root || !object.address.source
                                    ? object.address.object
                                    : object.address.source;
      auto name = effective.name.empty() ? display_uuid.string() : effective.name;
      if (!object.address.source) {
        result.nodes.push_back({.node = node,
                                .parent = parent_node,
                                .entity = entity,
                                .uuid = object.address.object,
                                .display_name = std::move(name),
                                .local_transform = effective.transform,
                                .components = std::move(effective.components)});
        continue;
      }
      prefab_node_record record;
      record.node = node;
      record.parent = parent_node;
      record.entity = entity;
      record.instance_uuid = object.address.object;
      record.source_node_uuid = object.address.source;
      record.display_name = std::move(name);
      record.prefab_uri = std::move(object.prefab_uri);
      record.local_transform = effective.transform;
      record.source_local_transform = object.source_transform;
      record.is_instance_root = object.instance_root;
      record.overrides = std::move(object.overrides);
      result.prefab_nodes.push_back(std::move(record));
    }
    return result;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

gneiss::engine::error
scene_edit_session::get_world_transform(scene_node_id node,
                                        gneiss::engine::math::transform& output) const noexcept {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  engine::math::transform result;
  std::set<scene_node_id> seen;
  try {
    while (node) {
      if (!seen.insert(node).second)
        return engine::error::invalid_state;
      auto ordinary = std::ranges::find(snapshot->nodes, node, &scene_node_record::node);
      auto prefab = std::ranges::find(snapshot->prefab_nodes, node, &prefab_node_record::node);
      if (ordinary != snapshot->nodes.end()) {
        result = engine::math::compose_transform(ordinary->local_transform, result);
        node = ordinary->parent;
      } else if (prefab != snapshot->prefab_nodes.end()) {
        result = engine::math::compose_transform(prefab->local_transform, result);
        node = prefab->parent;
      } else
        return engine::error::not_found;
    }
    output = result;
    return engine::error::success;
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

gneiss::engine::error
scene_edit_session::set_local_transform(scene_node_id node,
                                        const gneiss::engine::math::transform& value) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content([&](auto& description, auto& snapshot) -> engine::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    if (!target->source ||
        target->source == snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid) {
      object->transform = value;
    } else {
      auto source = engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
      if (!source)
        return source.error();
      auto candidate = engine::prepare_prefab_transform_edit(
          *source, *object->prefab, target->source, target->effective.transform, value);
      if (!candidate)
        return candidate.error();
      object->prefab = std::move(candidate->reference);
    }
    return engine::error::success;
  });
}

gneiss::engine::error scene_edit_session::restore_prefab_transform_field(
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

gneiss::engine::error scene_edit_session::restore_prefab_transform(
    scene_node_id node, gneiss::engine::math::transform& out_previous) noexcept {
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

gneiss::engine::error scene_edit_session::create_node(std::string_view name, scene_node_id parent,
                                                      scene_node_id& out_node) noexcept {
  out_node = {};
  auto parent_target = parent ? locate(parent) : engine::result<edit_target>{edit_target{}};
  if (!parent_target)
    return parent_target.error();
  engine::guid uuid;
  auto status = make_editor_uuid(uuid);
  if (status.failed())
    return status;
  return edit_content(
      [&](auto& description, auto&) -> engine::error {
        if (parent_target->source) {
          auto root = query_prefab_node(parent);
          if (!root || !root->is_instance_root)
            return engine::error::unsupported;
        }
        engine::game_object_description object;
        object.uuid = uuid;
        object.name = name;
        object.parent_uuid = parent_target->object;
        description.objects.push_back(std::move(object));
        return engine::error::success;
      },
      [&] {
        auto created = application_->scenes()->find_node(scene_, uuid);
        if (!created)
          return created.error();
        out_node = *created;
        selection_ = out_node;
        return engine::error{engine::error::success};
      });
}

gneiss::engine::error scene_edit_session::rename_node(scene_node_id node,
                                                      std::string_view name) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content([&](auto& description, auto& snapshot) -> engine::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    if (target->source &&
        target->source != snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid)
      return engine::error::unsupported;
    object->name = name;
    return engine::error::success;
  });
}

gneiss::engine::error scene_edit_session::reparent_node(scene_node_id node,
                                                        scene_node_id parent) noexcept {
  auto target = locate(node);
  auto destination = parent ? locate(parent) : engine::result<edit_target>{edit_target{}};
  if (!target)
    return target.error();
  if (!destination)
    return destination.error();
  return edit_content([&](auto& description, auto& snapshot) -> engine::error {
    for (const auto& item : {*target, *destination}) {
      if (!item.source)
        continue;
      auto object = std::ranges::find(description.objects, item.object,
                                      &engine::game_object_description::uuid);
      if (item.source != snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid)
        return engine::error::unsupported;
    }
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    object->parent_uuid = destination->object;
    return engine::error::success;
  });
}

engine::error scene_edit_session::destroy_subtree(scene_node_id node,
                                                  scene_subtree_snapshot& output) noexcept try {
  auto snapshot = capture_subtree(node);
  if (!snapshot)
    return snapshot.error();
  const auto result = edit_content([&](auto& description, auto&) -> engine::error {
    std::erase_if(description.objects, [&](const auto& object) {
      return std::ranges::find(snapshot->content.get().objects, object.uuid,
                               &engine::object_record::uuid) !=
             snapshot->content.get().objects.end();
    });
    return engine::error::success;
  });
  if (result.ok()) {
    output = std::move(*snapshot);
    selection_ = {};
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_edit_session::restore_subtree(const scene_subtree_snapshot& snapshot,
                                                  scene_node_id& output) noexcept {
  output = {};
  if (snapshot.parent_uuid) {
    auto ordinary = find_node(snapshot.parent_uuid);
    auto prefab = find_prefab_root(snapshot.parent_uuid);
    if (!ordinary && !prefab)
      return engine::error::not_found;
  }
  return edit_content(
      [&](auto& description, auto& next) {
        return import_subtree(snapshot, description, next, false);
      },
      [&] {
        auto created = application_->scenes()->find_node(scene_, snapshot.root_uuid);
        if (!created)
          return created.error();
        output = *created;
        selection_ = output;
        return engine::error{engine::error::success};
      });
}

engine::error scene_edit_session::duplicate_subtree(scene_node_id node, scene_node_id parent,
                                                    scene_node_id& output) noexcept try {
  auto captured = capture_subtree(node);
  if (!captured)
    return captured.error();
  auto target = parent ? locate(parent) : engine::result<edit_target>{edit_target{}};
  if (!target)
    return target.error();
  if (target->source) {
    auto info = query_prefab_node(parent);
    if (!info || !info->is_instance_root)
      return engine::error::unsupported;
  }
  auto snapshot = std::move(*captured);
  std::vector<engine::uuid_mapping> mapping;
  for (const auto& object : snapshot.content.get().objects)
    mapping.push_back({object.uuid, make_uuid_value()});
  auto remapped = engine::remap_scene_record(
      snapshot.content, document_->current().content().content.get().uuid, mapping);
  if (!remapped)
    return remapped.error();
  auto copied = engine::restore_scene_record(*remapped);
  if (!copied)
    return copied.error();
  for (auto& object : copied->objects)
    if (object.components.template get<engine::camera_description>()) {
      auto status = object.components.template update<engine::camera_description>(
          [](auto& camera) { camera.is_primary = false; });
      if (status.failed())
        return status;
    }
  auto frozen = engine::capture_scene_record(*copied);
  if (!frozen)
    return frozen.error();
  snapshot.content = std::move(*frozen);
  std::map<engine::guid, engine::scene_object_resources> resources;
  for (const auto& pair : mapping) {
    if (auto item = snapshot.resources.find(pair.source_uuid); item != snapshot.resources.end())
      resources.emplace(pair.target_uuid, item->second);
    if (snapshot.root_uuid == pair.source_uuid)
      snapshot.root_uuid = pair.target_uuid;
  }
  snapshot.resources = std::move(resources);
  snapshot.parent_uuid = target->object;
  return restore_subtree(snapshot, output);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::result<engine::component_descriptions>
scene_edit_session::components(scene_node_id node) const noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return target->effective.components;
}
engine::result<prefab_component_state>
scene_edit_session::capture_component_override(scene_node_id node, engine::guid type) noexcept {
  try {
    auto target = locate(node);
    if (!target)
      return target.error();
    if (!target->source)
      return engine::error::unsupported;
    auto description = document_->current().content().description();
    if (!description)
      return description.error();
    auto object = std::ranges::find(description->objects, target->object,
                                    &engine::game_object_description::uuid);
    prefab_component_state result;
    for (const auto& item : object->prefab->component_overrides)
      if (item.source_node_uuid == target->source && item.type_guid == type) {
        result.added = item.added;
        result.component_uuid = item.component_uuid;
        result.removed = !item.added;
      }
    for (const auto& item : object->prefab->overrides)
      if (item.key.target.source_node_uuid == target->source &&
          item.key.target.target_type_guid == type)
        result.properties.push_back(item);
    if (type == engine::mesh_renderer_description::type_guid) {
      auto binding = capture_mesh_binding(node);
      if (!binding)
        return binding.error();
      result.resources = *binding;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error
scene_edit_session::set_component_override(scene_node_id node, engine::guid type,
                                           const prefab_component_state& state) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  if (!target->source)
    return engine::error::unsupported;
  if (state.added && !*state.added)
    return engine::error::invalid_argument;
  return edit_content([&](auto& description, auto& snapshot) -> engine::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    auto source = engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
    if (!source)
      return source.error();
    auto candidate = engine::prepare_prefab_component_override(
        *source, *object->prefab, target->source, type,
        state.added ? *state.added : engine::component_description{}, state.removed,
        state.properties, state.component_uuid);
    if (!candidate)
      return candidate.error();
    const auto member = std::ranges::find(candidate->effective.objects, target->source,
                                          &engine::game_object_description::uuid);
    if (member == candidate->effective.objects.end())
      return engine::error::not_found;
    object->prefab = std::move(candidate->reference);
    if (state.resources.mesh)
      snapshot.assets.insert_or_assign(state.resources.mesh->source().uri, state.resources.mesh);
    if (state.resources.material)
      snapshot.assets.insert_or_assign(state.resources.material->source().uri,
                                       state.resources.material);
    return engine::error::success;
  });
}
engine::error
scene_edit_session::add_component_description(scene_node_id node,
                                              const engine::component_description& value) noexcept {
  return value ? edit_component(node, value->guid(), value, 1)
               : engine::error{engine::error::invalid_argument};
}
engine::error scene_edit_session::remove_component_description(scene_node_id node,
                                                               engine::guid type) noexcept {
  return edit_component(node, type, {}, -1);
}
engine::error
scene_edit_session::set_component_description(scene_node_id node,
                                              const engine::component_description& value) noexcept {
  return value ? edit_component(node, value->guid(), value, 0)
               : engine::error{engine::error::invalid_argument};
}

gneiss::engine::error
scene_edit_session::set_camera(scene_node_id node,
                               const gneiss::engine::camera_description& desc) noexcept {
  try {
    auto current = components(node);
    if (!current)
      return current.error();
    return edit_component(node, engine::camera_description::type_guid,
                          engine::make_component_description(desc),
                          current->get<engine::camera_description>() ? 0 : 1);
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

gneiss::engine::error scene_edit_session::remove_camera(scene_node_id node) noexcept {
  return remove_component_description(node, engine::camera_description::type_guid);
}

gneiss::engine::error scene_edit_session::remove_mesh_renderer(scene_node_id node) noexcept {
  return remove_component_description(node, engine::mesh_renderer_description::type_guid);
}

void scene_edit_session::close() noexcept {
  selection_ = {};
  uri_.clear();
  if (is_open())
    (void)with_scene([&](auto& service) { return service.unload(scene_); });
  document_.reset();
  projection_ = {};
  scene_ = {};
  application_ = nullptr;
  world_ = 0;
}

engine::result<scene_node_record> scene_edit_session::query_node(scene_node_id node) const noexcept
    try {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->nodes)
    if (item.node == node && node)
      return std::move(item);
  return engine::error::not_found;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_edit_session::query_prefab_node(scene_node_id node) const noexcept try {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.node == node && node)
      return std::move(item);
  return engine::error::not_found;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_node_record> scene_edit_session::find_node(engine::guid uuid) const noexcept
    try {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->nodes)
    if (item.uuid == uuid)
      return std::move(item);
  return engine::error::not_found;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_edit_session::find_prefab_root(engine::guid uuid) const noexcept try {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.instance_uuid == uuid && item.is_instance_root)
      return std::move(item);
  return engine::error::not_found;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_node_record>
scene_edit_session::find_prefab_source(engine::guid uuid, engine::guid source) const noexcept try {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.instance_uuid == uuid && item.source_node_uuid == source)
      return std::move(item);
  return engine::error::not_found;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_node_record> scene_edit_session::selected_node() const noexcept {
  return query_node(selection_);
}
engine::result<prefab_node_record> scene_edit_session::selected_prefab_node() const noexcept {
  return query_prefab_node(selection_);
}
engine::error scene_edit_session::select(scene_node_id node) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  if (node) {
    auto target = locate(node);
    if (!target)
      return target.error();
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
engine::error scene_edit_session::validate_selection() noexcept {
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
scene_edit_session::capture_mesh_binding(scene_node_id node) const noexcept {
  try {
    auto values = components(node);
    if (!values)
      return values.error();
    const auto* renderer = values->get<engine::mesh_renderer_description>();
    if (!renderer)
      return engine::scene_mesh_binding{};
    engine::scene_mesh_binding result;
    const auto& assets = document_->current().content().assets;
    if (auto found = assets.find(renderer->mesh_uri); found != assets.end())
      result.mesh = engine::resource_cast<engine::mesh>(found->second);
    if (auto found = assets.find(renderer->material_uri); found != assets.end())
      result.material = engine::resource_cast<engine::material>(found->second);
    return result;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::result<scene_node_id>
scene_edit_session::create_mesh_node(const mesh_node_creation& snapshot,
                                     const engine::scene_mesh_binding& resources) noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  if (snapshot.uuid.is_nil())
    return engine::error::invalid_argument;
  if (engine::resource_uri(resources.mesh) != snapshot.mesh_uri ||
      engine::resource_uri(resources.material) != snapshot.material_uri)
    return engine::error::invalid_argument;
  if (snapshot.parent_uuid) {
    auto found = find_node(snapshot.parent_uuid);
    if (!found)
      return found.error();
  }
  scene_node_id created;
  auto status = edit_content(
      [&](auto& description, auto& next) -> engine::error {
        engine::game_object_description object;
        object.uuid = snapshot.uuid;
        object.parent_uuid = snapshot.parent_uuid;
        object.name = snapshot.display_name;
        object.components.set(
            engine::mesh_renderer_description{snapshot.mesh_uri, snapshot.material_uri});
        description.objects.push_back(std::move(object));
        retain(next, resources);
        return engine::error::success;
      },
      [&] {
        auto result = application_->scenes()->find_node(scene_, snapshot.uuid);
        if (!result)
          return result.error();
        created = *result;
        selection_ = created;
        return engine::error{engine::error::success};
      });
  if (status.failed())
    return status;
  return created;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error scene_edit_session::destroy_node(scene_node_id node) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  for (const auto& object : document_->current().content().content.get().objects)
    if (object.parent == target->object)
      return engine::error::invalid_state;
  scene_subtree_snapshot ignored;
  return destroy_subtree(node, ignored);
}

gneiss::engine::error
scene_edit_session::set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                      std::string_view material_uri) noexcept {
  try {
    auto current = components(node);
    if (!current)
      return current.error();
    return edit_component(node, engine::mesh_renderer_description::type_guid,
                          engine::make_component_description(engine::mesh_renderer_description{
                              std::string(mesh_uri), std::string(material_uri)}),
                          current->get<engine::mesh_renderer_description>() ? 0 : 1);
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

engine::error
scene_edit_session::replace_mesh_binding(scene_node_id node,
                                         const engine::scene_mesh_binding& expected,
                                         const engine::scene_mesh_binding& replacement) noexcept {
  auto current = capture_mesh_binding(node);
  if (!current)
    return current.error();
  if (current->mesh != expected.mesh || current->material != expected.material)
    return engine::error::invalid_state;
  auto target = locate(node);
  if (!target)
    return target.error();
  if (target->source)
    return engine::error::unsupported;
  return edit_content([&](auto& description, auto& next) -> engine::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    object->components.set(
        engine::mesh_renderer_description{std::string(engine::resource_uri(replacement.mesh)),
                                          std::string(engine::resource_uri(replacement.material))});
    retain(next, replacement);
    return engine::error::success;
  });
}

engine::result<std::string> scene_edit_session::serialize() const noexcept {
  std::string output;
  auto status = encode_scene(output);
  return status.ok() ? engine::result<std::string>{std::move(output)}
                     : engine::result<std::string>{status};
}

engine::error scene_edit_session::encode_scene(std::string& output) const noexcept try {
  auto snapshot = capture_content();
  if (!snapshot)
    return snapshot.error();
  auto description = snapshot->description();
  if (!description)
    return description.error();
  auto encoded = engine::asset_file::serialize_bytes(*description);
  if (!encoded)
    return encoded.error().error;
  output.assign(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_edit_session::begin_save(gneiss::engine::files::service& files,
                                             std::string_view uri,
                                             gneiss::engine::files::write_mode mode,
                                             std::shared_ptr<save_receipt>& output) noexcept {
  if (edit_base_)
    return engine::error::invalid_state;
  output.reset();
  if (!is_open() || !uri.starts_with("asset://") || engine::validate_uri(uri).failed())
    return engine::error::invalid_argument;
  try {
    auto receipt = std::make_shared<save_receipt>();
    receipt->scene = scene_;
    receipt->version = document_->current();
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
engine::error scene_edit_session::accept_save(const save_receipt& receipt) noexcept {
  if (edit_base_)
    return engine::error::invalid_state;
  if (!receipt.complete)
    return engine::error::not_ready;
  if (receipt.status.failed())
    return receipt.status;
  if (!is_open() || scene_ != receipt.scene)
    return engine::error::invalid_state;
  try {
    auto saved = document_->mark_saved(receipt.version);
    if (saved.failed())
      return saved;
    uri_ = receipt.uri;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_edit_session::restore_changes(const scene_document::revision& expected,
                                                  const scene_document::revision& target,
                                                  std::span<const document_patch> changes,
                                                  bool undo) noexcept try {
  if (!is_open() || document_->current().stamp() != expected)
    return engine::error::invalid_state;
  auto before = document_->current();
  auto next = before.content();
  for (std::size_t i = 0; i < changes.size(); ++i) {
    auto result = apply_document_patch(next, changes[undo ? changes.size() - 1 - i : i], undo);
    if (!result)
      return result.error();
    next = std::move(*result);
  }
  const bool synchronized = projection_.is_synchronized(document_->current());
  auto status = document_->checkout(expected, target, next);
  if (status.failed())
    return status;
  status = project_scene_document(*world_, *application_->scenes(), scene_, projection_,
                                  synchronized ? &before.content() : nullptr, next);
  (void)report_projection(document_->current(), status);
  (void)validate_selection();
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_edit_session::restore_version(const version& expected,
                                                  const version& target) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  const bool synchronized = projection_.is_synchronized(document_->current());
  auto status = document_->checkout(expected, target);
  if (status.failed())
    return status;
  if (synchronized) {
    status = project_scene_document(*world_, *application_->scenes(), scene_, projection_,
                                    &expected.content(), target.content());
    (void)report_projection(target, status);
    (void)validate_selection();
  } else {
    (void)synchronize();
  }
  return engine::error::success;
}

engine::error scene_edit_session::edit_component(scene_node_id node, engine::guid type,
                                                 const engine::component_description& value,
                                                 int mode) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  const auto* current = target->effective.components.find(type);
  if (mode == 1 && current)
    return engine::error::invalid_state;
  if (mode != 1 && !current)
    return engine::error::not_found;
  engine::guid identity;
  return edit_content([&](auto& description, auto& snapshot) -> engine::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &engine::game_object_description::uuid);
    if (mode == 1)
      identity = engine::guid::generate();
    if (target->source) {
      auto source = engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
      if (!source)
        return source.error();
      auto local = std::ranges::find_if(object->prefab->component_overrides, [&](const auto& item) {
        return item.source_node_uuid == target->source && item.type_guid == type &&
               item.added.has_value();
      });
      auto candidate =
          mode == 0
              ? engine::prepare_prefab_component_edit(*source, *object->prefab, target->source,
                                                      *current, value)
              : engine::prepare_prefab_component_override(
                    *source, *object->prefab, target->source, type,
                    mode == 1 ? value : engine::component_description{},
                    mode == -1 && local == object->prefab->component_overrides.end(), {}, identity);
      if (!candidate)
        return candidate.error();
      object->prefab = std::move(candidate->reference);
    } else {
      if (mode == -1)
        object->components.erase(type);
      else if (mode == 1) {
        auto entries = object->components.entries();
        auto identified = engine::with_component_identity(value, identity);
        if (!identified)
          return identified.error();
        entries.push_back(std::move(*identified));
        auto status = object->components.replace(std::move(entries));
        if (status.failed())
          return status;
      } else
        object->components.set(value);
      if (value && type == engine::camera_description::type_guid &&
          value->get<engine::camera_description>()->is_primary) {
        for (auto& other : description.objects) {
          if (other.uuid == object->uuid)
            continue;
          if (other.components.template get<engine::camera_description>()) {
            auto status = other.components.template update<engine::camera_description>(
                [](auto& camera) { camera.is_primary = false; });
            if (status.failed())
              return status;
          }
        }
      }
    }
    return engine::error::success;
  });
}

engine::result<scene_subtree_snapshot>
scene_edit_session::capture_subtree(scene_node_id node) const noexcept try {
  auto target = locate(node);
  if (!target)
    return target.error();
  auto snapshot = capture_content();
  if (!snapshot)
    return snapshot.error();
  auto description = snapshot->description();
  if (!description)
    return description.error();
  if (target->source) {
    auto info = query_prefab_node(node);
    if (!info || !info->is_instance_root)
      return engine::error::unsupported;
  }
  scene_subtree_snapshot result;
  result.root_uuid = target->object;
  std::set<engine::guid> included{target->object};
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto& object : description->objects)
      if (included.contains(object.parent_uuid))
        changed |= included.insert(object.uuid).second;
  }
  std::erase_if(description->objects,
                [&](const auto& object) { return !included.contains(object.uuid); });
  for (auto& object : description->objects) {
    if (object.uuid == result.root_uuid) {
      result.parent_uuid = object.parent_uuid;
      object.parent_uuid = {};
    }
    if (!object.prefab && !object.components.template get<engine::mesh_renderer_description>())
      continue;
    auto& resources = result.resources[object.uuid];
    if (object.prefab) {
      const auto& frozen = snapshot->prefabs.at(object.prefab->uri);
      resources.prefab_content = frozen.content;
      resources.source_asset = frozen.source_asset;
      auto source = engine::restore_prefab_record(frozen.content);
      if (!source)
        return source.error();
      auto effective = engine::replay_prefab_reference(*source, *object.prefab);
      if (!effective)
        return effective.error();
      for (const auto& member : effective->description.objects)
        resources.source_nodes.push_back(binding_for(member.components, *snapshot));
    } else
      resources.renderer = binding_for(object.components, *snapshot);
  }
  auto content = engine::capture_scene_record(*description);
  if (!content)
    return content.error();
  result.content = std::move(*content);
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prefab_header_snapshot>
scene_edit_session::capture_prefab_header(engine::guid uuid) const noexcept try {
  auto snapshot = capture_content();
  if (!snapshot)
    return snapshot.error();
  auto description = snapshot->description();
  if (!description)
    return description.error();
  auto object =
      std::ranges::find(description->objects, uuid, &engine::game_object_description::uuid);
  if (object == description->objects.end() || !object->prefab)
    return engine::error::not_found;
  const auto& frozen = snapshot->prefabs.at(object->prefab->uri);
  auto source = engine::restore_prefab_record(frozen.content);
  if (!source)
    return source.error();
  return prefab_header_snapshot{
      *object, engine::make_ref<const engine::prefab_description>(std::move(*source)),
      frozen.source_asset};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_edit_session::apply_prefab(const scene_subtree_snapshot& snapshot,
                                               bool removing, bool replacing) noexcept {
  return edit_content(
      [&](auto& description, auto& next) -> engine::error {
        if (!removing)
          return import_subtree(snapshot, description, next, replacing);
        if (replacing)
          return engine::error::invalid_argument;
        std::erase_if(description.objects, [&](const auto& object) {
          return std::ranges::find(snapshot.content.get().objects, object.uuid,
                                   &engine::object_record::uuid) !=
                 snapshot.content.get().objects.end();
        });
        return engine::error::success;
      },
      [&] {
        if (removing) {
          (void)validate_selection();
          return engine::error{engine::error::success};
        }
        auto root = application_->scenes()->find_node(scene_, snapshot.root_uuid);
        if (!root)
          return root.error();
        selection_ = *root;
        return engine::error{engine::error::success};
      });
}
} // namespace gneiss::editor
