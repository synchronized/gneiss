// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include "documents/components/component_registration.hpp"
#include "documents/scene_projection.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/asset_file.hpp>

#include <gneiss/engine/core/meta/transform_properties.hpp>

#include <gneiss/engine/resource/asset_uri.hpp>

#include "documents/prefab_edit.hpp"
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
// 文档 UUID 只能通过投影关联查找，不能作为运行对象 UUID 使用。
gneiss::result<entity_id> document_root(const scene_edit_session& session, gneiss::guid uuid) {
  auto snapshot = session.capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (const auto& object : snapshot->nodes)
    if (object.uuid == uuid)
      return object.node;
  for (const auto& object : snapshot->prefab_nodes)
    if (object.instance_uuid == uuid && object.is_instance_root)
      return object.node;
  return gneiss::error::not_found;
}
gneiss::result<entity_id> document_node(const scene_edit_session& session,
                                        gneiss::object_address address) {
  if (!address.object)
    return address.source ? gneiss::result<entity_id>{gneiss::error::invalid_argument}
                          : gneiss::result<entity_id>{entity_id{}};
  if (!address.source)
    return document_root(session, address.object);
  auto member = session.find_prefab_source(address.object, address.source);
  if (!member)
    return member.error();
  return member->node;
}
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

[[nodiscard]] gneiss::guid make_uuid_value() { return gneiss::guid::generate(); }

scene_mesh_binding binding_for(const gneiss::component_descriptions& components,
                               const scene_document_snapshot& snapshot) {
  scene_mesh_binding result;
  const auto* renderer = components.get<gneiss::mesh_component_description>();
  if (!renderer)
    return result;
  if (auto found = snapshot.assets.find(renderer->mesh_uri); found != snapshot.assets.end())
    result.mesh = gneiss::resource_cast<gneiss::mesh>(found->second);
  if (auto found = snapshot.assets.find(renderer->material_uri); found != snapshot.assets.end())
    result.material = gneiss::resource_cast<gneiss::material>(found->second);
  return result;
}
void retain(scene_document_snapshot& snapshot, const scene_mesh_binding& binding) {
  if (binding.mesh)
    snapshot.assets.insert_or_assign(binding.mesh->source().uri, binding.mesh);
  if (binding.material)
    snapshot.assets.insert_or_assign(binding.material->source().uri, binding.material);
}
gneiss::error import_subtree(const scene_subtree_snapshot& subtree,
                             gneiss::scene_description& description,
                             scene_document_snapshot& snapshot, bool replacing) {
  auto incoming = subtree.description();
  if (!incoming)
    return incoming.error();
  for (auto object : incoming->objects) {
    if (object.prefab && !subtree.resources.contains(object.uuid))
      return gneiss::error::not_found;
    if (object.uuid == subtree.root_uuid)
      object.parent = subtree.parent;
    auto existing =
        std::ranges::find(description.objects, object.uuid, &gneiss::game_object_description::uuid);
    if (existing != description.objects.end()) {
      if (!replacing)
        return gneiss::error::invalid_argument;
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
  return gneiss::error::success;
}
} // namespace

gneiss::error make_editor_uuid(gneiss::guid& output) {
  {
    output = make_uuid_value();
    return gneiss::error::success;
  }
}

gneiss::error make_asset_uri(const std::filesystem::path& asset_root,
                                     const std::filesystem::path& path, std::string& output) {
  if (asset_root.empty() || path.empty()) {
    return gneiss::error::invalid_argument;
  }
  try {
    std::error_code error;
    const auto root = std::filesystem::canonical(asset_root, error);
    if (error) {
      return gneiss::error::io;
    }
    const auto target = std::filesystem::weakly_canonical(path, error);
    if (error || !is_within(root, target)) {
      return gneiss::error::invalid_argument;
    }
    const auto relative = std::filesystem::relative(target, root, error).generic_u8string();
    if (error || relative.empty()) {
      return gneiss::error::invalid_argument;
    }
    std::string uri = "asset://";
    uri.append(reinterpret_cast<const char*>(relative.data()), relative.size());
    if (gneiss::validate_uri(uri) != gneiss::error::success) {
      return gneiss::error::invalid_argument;
    }
    output = std::move(uri);
    return gneiss::error::success;
  } catch (const std::filesystem::filesystem_error&) {
    return gneiss::error::io;
  }
}

bool scene_edit_session::is_open() const noexcept {
  if (!application_ || !scene_.id() || !application_->is_owner_thread() ||
      !application_->accepts_requests() || application_->native_world() != world_)
    return false;
  // World 地址可能复用，必须同时校验实例 ID 的所属域及代次。
  return document_ && bool(document_->current()) && scene_.is_valid();
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
    scene_ = std::exchange(other.scene_, scene_instance{});
    selection_ = std::exchange(other.selection_, {});
    uri_ = std::move(other.uri_);
  }
  return *this;
}

gneiss::error scene_edit_session::adopt(runtime::application& application,
                                        gneiss::scene_instance scene,
                                        const scene_document_snapshot& initial,
                                        std::span<const gneiss::scene_object_binding> objects) {
  if (edit_base_ || !application.is_owner_thread() || !application.accepts_requests())
    return gneiss::error::invalid_state;
  auto* world = scene.owner();
  if (!world || world != application.native_world() || initial.uri.empty() || !scene.id())
    return gneiss::error::invalid_argument;
  {
    if (!scene.is_valid())
      return gneiss::error::invalid_handle;
    if (scene.size() != objects.size())
      return gneiss::error::invalid_argument;
    for (const auto& binding : objects) {
      auto object = world->object(binding.entity);
      if (!object)
        return object.error();
      if (!scene.contains(*object))
        return gneiss::error::invalid_handle;
    }
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
      refreshed = pending.report_projection(pending.document_->current(), gneiss::error::success);
    if (refreshed != gneiss::error::success) {
      // 失败时仍由调用方持有场景，不改变当前文档。
      pending.scene_ = {};
      return refreshed;
    }
    *this = std::move(pending);
    return gneiss::error::success;
  }
}

gneiss::error scene_edit_session::create_empty(runtime::application& application,
                                                       gneiss::world* world,
                                                       gneiss::guid scene_uuid) {
  if (edit_base_ || !application.is_owner_thread() || !application.accepts_requests())
    return gneiss::error::invalid_state;
  if (!world || world != application.native_world() || scene_uuid.is_nil())
    return gneiss::error::invalid_argument;
  auto created = world->create_scene();
  if (!created)
    return created.error();
  gneiss::scene_description description;
  description.uuid = scene_uuid;
  gneiss::scene_content initial;
  initial.description = gneiss::make_ref<const gneiss::scene_description>(std::move(description));
  auto frozen = make_scene_document_snapshot({}, initial);
  if (!frozen) {
    created->unload();
    return frozen.error();
  }
  std::unique_ptr<scene_document> versions;
  try {
    versions = std::make_unique<scene_document>();
  } catch (...) {
    // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
    created->unload();
    throw;
  }
  const auto status = versions->reset(*frozen, false);
  if (status.failed()) {
    created->unload();
    return status;
  }
  close();
  document_ = std::move(versions);
  application_ = &application;
  scene_ = *created;
  world_ = world;
  const auto projected = report_projection(document_->current(), gneiss::error::success);
  if (projected.failed()) {
    close();
    return projected;
  }
  return gneiss::error::success;
}

gneiss::error scene_edit_session::create_empty(runtime::application& application,
                                                       gneiss::world* world) {
  {
    return create_empty(application, world, make_uuid_value());
  }
}

gneiss::result<scene_document_snapshot> scene_edit_session::capture_content() const {
  if (!is_open())
    return gneiss::error::invalid_state;
  auto snapshot = document_->current().content();
  snapshot.uri = uri_;
  return snapshot;
}
gneiss::error scene_edit_session::projection_status() const noexcept {
  return document_ ? projection_.status(document_->current())
                   : gneiss::error{gneiss::error::invalid_state};
}
gneiss::error scene_edit_session::accept_content(const scene_document_snapshot& snapshot) {
  if (!is_open())
    return gneiss::error::invalid_state;
  if (snapshot.uri != uri_)
    return gneiss::error::invalid_argument;
  const auto before = document_->current();
  const bool synchronized = projection_.is_synchronized(before);
  const auto selected = projection_.address(selection_);
  auto status = document_->accept(before, snapshot);
  if (status.failed())
    return status;
  status = project_scene_document(scene_, projection_, synchronized ? &before.content() : nullptr,
                                  snapshot);
  (void)report_projection(document_->current(), status);
  if (selected) {
    auto found = projection_.objects().find(*selected);
    selection_ = found == projection_.objects().end() ? entity_id{} : found->second.entity;
  }
  return gneiss::error::success;
}
gneiss::error scene_edit_session::synchronize() try {
  if (!is_open())
    return gneiss::error::invalid_state;
  const auto version = document_->current();
  auto selected = locate(selection_);
  auto status = project_scene_document(scene_, projection_, nullptr, version.content());
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
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  if (document_)
    (void)report_projection(document_->current(), gneiss::error::internal);
  throw;
}
gneiss::error
scene_edit_session::edit_content(const content_edit& edit,
                                 const std::function<gneiss::error()>& after_projection) {
  auto next = capture_content();
  if (!next)
    return next.error();
  auto description = next->description();
  if (!description)
    return description.error();
  auto status = edit(*description, *next);
  if (status.failed())
    return status;
  auto content = gneiss::capture_scene_record(*description);
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
      selection_ ? locate(selection_) : gneiss::result<edit_target>{gneiss::error::not_found};
  status = document_->accept(before, prepared->snapshot);
  if (status.failed())
    return status;
  auto projection =
      project_scene_document(scene_, projection_, synchronized ? &before.content() : nullptr,
                             document_->current().content(), false);
  if (projection.ok()) {
    if (selected) {
      if (selected->source) {
        auto found = find_prefab_source(selected->object, selected->source);
        selection_ = found ? found->node : entity_id{};
      } else {
        auto found = find_node(selected->object);
        selection_ = found ? found->node : entity_id{};
      }
    }
    if (after_projection) {
      {
        projection = after_projection();
      }
    }
  }
  (void)report_projection(document_->current(), projection);
  return gneiss::error::success;
}
gneiss::result<scene_edit_session::edit_target> scene_edit_session::locate(entity_id node) const {
  if (!is_open())
    return gneiss::error::invalid_state;
  if (!node)
    return gneiss::error::invalid_handle;
  auto identity = projection_.address(node);
  auto object = world_->object(entity_id{node.get()});
  if (!identity || !object || object->entity() != projection_.objects().at(*identity).entity)
    return gneiss::error::not_found;
  auto composed = compose_scene_document(document_->current().content());
  if (!composed)
    return composed.error();
  const auto address = *identity;
  auto found = std::ranges::find(composed->objects, address, &gneiss::composed_object::address);
  if (found == composed->objects.end())
    return gneiss::error::not_found;
  return edit_target{address.object, address.source, std::move(found->effective)};
}

gneiss::error scene_edit_session::report_projection(const version& projected,
                                                    gneiss::error status) noexcept {
  if (!document_ || document_->current() != projected)
    return gneiss::error::invalid_state;

  (void)projection_.report(document_->current(), projected, status);
  return status;
}
gneiss::result<scene_query_snapshot> scene_edit_session::capture_nodes() const {
  {
    auto snapshot = capture_content();
    if (!snapshot)
      return snapshot.error();
    auto composed = compose_scene_document(*snapshot);
    if (!composed)
      return composed.error();
    auto document = snapshot->description();
    if (!document)
      return document.error();
    std::map<std::string, gneiss::prefab_description> sources;
    for (const auto& [uri, source] : snapshot->prefabs) {
      auto restored = gneiss::restore_prefab_record(source.content);
      if (!restored)
        return restored.error();
      sources.emplace(uri, std::move(*restored));
    }
    const auto& projected = projection_.objects();
    scene_query_snapshot result;
    for (auto& object : composed->objects) {
      auto found = projected.find(object.address);
      auto parent = projected.find(object.parent);
      const auto node = found == projected.end() ? entity_id{} : found->second.entity;
      const auto parent_node = parent == projected.end() ? entity_id{} : parent->second.entity;
      const auto entity = found == projected.end() ? entity_id{} : found->second.entity;
      auto& effective = object.effective;
      const gneiss::game_object_description* author = nullptr;
      const gneiss::prefab_description* source = nullptr;
      bool instance_root = false;
      if (object.address.source) {
        auto found_author = std::ranges::find(document->objects, object.address.object,
                                              &gneiss::game_object_description::uuid);
        if (found_author == document->objects.end() || !found_author->prefab)
          return gneiss::error::invalid_state;
        author = &*found_author;
        auto found_source = sources.find(author->prefab->uri);
        if (found_source == sources.end())
          return gneiss::error::not_found;
        source = &found_source->second;
        instance_root = object.address.source == source->root_uuid;
      }
      const auto display_uuid =
          instance_root || !object.address.source ? object.address.object : object.address.source;
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
      record.prefab_uri = author->prefab->uri;
      record.local_transform = effective.transform;
      const auto original = std::ranges::find(source->objects, object.address.source,
                                              &gneiss::game_object_description::uuid);
      if (original == source->objects.end())
        return gneiss::error::not_found;
      record.source_local_transform = original->transform;
      record.is_instance_root = instance_root;
      for (const auto& change : author->prefab->overrides)
        if (change.key.target.source_node_uuid == object.address.source)
          record.overrides.push_back(change.key);
      result.prefab_nodes.push_back(std::move(record));
    }
    return result;
  }
}

gneiss::error
scene_edit_session::get_world_transform(entity_id node,
                                        gneiss::math::transform& output) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  gneiss::math::transform result;
  std::set<entity_id> seen;
  {
    while (node) {
      if (!seen.insert(node).second)
        return gneiss::error::invalid_state;
      auto ordinary = std::ranges::find(snapshot->nodes, node, &scene_node_record::node);
      auto prefab = std::ranges::find(snapshot->prefab_nodes, node, &prefab_node_record::node);
      if (ordinary != snapshot->nodes.end()) {
        result = gneiss::math::compose_transform(ordinary->local_transform, result);
        node = ordinary->parent;
      } else if (prefab != snapshot->prefab_nodes.end()) {
        result = gneiss::math::compose_transform(prefab->local_transform, result);
        node = prefab->parent;
      } else
        return gneiss::error::not_found;
    }
    output = result;
    return gneiss::error::success;
  }
}

gneiss::error
scene_edit_session::set_local_transform(entity_id node,
                                        const gneiss::math::transform& value) {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content([&](auto& description, auto& snapshot) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    if (!target->source ||
        target->source == snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid) {
      object->transform = value;
    } else {
      auto source = gneiss::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
      if (!source)
        return source.error();
      auto candidate = prepare_prefab_transform_edit(*source, *object->prefab, target->source,
                                                     target->effective.transform, value);
      if (!candidate)
        return candidate.error();
      object->prefab = std::move(candidate->reference);
    }
    return gneiss::error::success;
  });
}

gneiss::error
scene_edit_session::restore_prefab_transform_field(entity_id node, std::uint32_t field_id,
                                                   gneiss::math::transform& out_previous) {
  const auto found = query_prefab_node(node);
  if (!found || found->is_instance_root) {
    return gneiss::error::invalid_argument;
  }
  auto restored = found->local_transform;
  switch (field_id) {
  case gneiss::meta::transform_translation_field.get():
    restored.translation = found->source_local_transform.translation;
    break;
  case gneiss::meta::transform_rotation_field.get():
    restored.rotation = found->source_local_transform.rotation;
    break;
  case gneiss::meta::transform_scale_field.get():
    restored.scale = found->source_local_transform.scale;
    break;
  default:
    return gneiss::error::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, restored);
  if (operation == gneiss::error::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::error
scene_edit_session::restore_prefab_transform(entity_id node,
                                             gneiss::math::transform& out_previous) {
  const auto found = query_prefab_node(node);
  if (!found || found->is_instance_root) {
    return gneiss::error::invalid_argument;
  }
  const auto previous = found->local_transform;
  const auto operation = set_local_transform(node, found->source_local_transform);
  if (operation == gneiss::error::success) {
    out_previous = previous;
  }
  return operation;
}

gneiss::error scene_edit_session::create_node(std::string_view name, entity_id parent,
                                                      entity_id& out_node) {
  out_node = {};
  auto parent_target = parent ? locate(parent) : gneiss::result<edit_target>{edit_target{}};
  if (!parent_target)
    return parent_target.error();
  gneiss::guid uuid;
  auto status = make_editor_uuid(uuid);
  if (status.failed())
    return status;
  return edit_content(
      [&](auto& description, auto&) -> gneiss::error {
        gneiss::game_object_description object;
        object.uuid = uuid;
        object.name = name;
        object.parent = {parent_target->object, parent_target->source};
        description.objects.push_back(std::move(object));
        return gneiss::error::success;
      },
      [&] {
        auto created = document_root(*this, uuid);
        if (!created)
          return created.error();
        out_node = *created;
        selection_ = out_node;
        return gneiss::error{gneiss::error::success};
      });
}

gneiss::error scene_edit_session::rename_node(entity_id node, std::string_view name) {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content([&](auto& description, auto& snapshot) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    if (target->source &&
        target->source != snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid)
      return gneiss::error::unsupported;
    object->name = name;
    return gneiss::error::success;
  });
}

gneiss::error scene_edit_session::reparent_node(entity_id node, entity_id parent) {
  auto target = locate(node);
  auto destination = parent ? locate(parent) : gneiss::result<edit_target>{edit_target{}};
  if (!target)
    return target.error();
  if (!destination)
    return destination.error();
  return edit_content([&](auto& description, auto& snapshot) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    if (target->source &&
        target->source != snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid)
      return gneiss::error::unsupported;
    object->parent = {destination->object, destination->source};
    return gneiss::error::success;
  });
}

gneiss::error scene_edit_session::destroy_subtree(entity_id node, scene_subtree_snapshot& output) {
  auto snapshot = capture_subtree(node);
  if (!snapshot)
    return snapshot.error();
  const auto result = edit_content([&](auto& description, auto&) -> gneiss::error {
    std::erase_if(description.objects, [&](const auto& object) {
      return std::ranges::find(snapshot->content.get().objects, object.uuid,
                               &gneiss::object_record::uuid) !=
             snapshot->content.get().objects.end();
    });
    return gneiss::error::success;
  });
  if (result.ok()) {
    output = std::move(*snapshot);
    selection_ = {};
  }
  return result;
}

gneiss::error scene_edit_session::restore_subtree(const scene_subtree_snapshot& snapshot,
                                                  entity_id& output) {
  output = {};
  auto parent = document_node(*this, snapshot.parent);
  if (!parent)
    return parent.error();
  return edit_content(
      [&](auto& description, auto& next) {
        return import_subtree(snapshot, description, next, false);
      },
      [&] {
        auto created = document_root(*this, snapshot.root_uuid);
        if (!created)
          return created.error();
        output = *created;
        selection_ = output;
        return gneiss::error{gneiss::error::success};
      });
}

gneiss::error scene_edit_session::duplicate_subtree(entity_id node, entity_id parent,
                                                    entity_id& output) {
  auto captured = capture_subtree(node);
  if (!captured)
    return captured.error();
  auto target = parent ? locate(parent) : gneiss::result<edit_target>{edit_target{}};
  if (!target)
    return target.error();
  auto snapshot = std::move(*captured);
  std::vector<gneiss::uuid_mapping> mapping;
  for (const auto& object : snapshot.content.get().objects)
    mapping.push_back({object.uuid, make_uuid_value()});
  auto remapped = gneiss::remap_scene_record(
      snapshot.content, document_->current().content().content.get().uuid, mapping);
  if (!remapped)
    return remapped.error();
  auto copied = gneiss::restore_scene_record(*remapped);
  if (!copied)
    return copied.error();
  for (auto& object : copied->objects)
    if (object.components.template get<gneiss::camera_description>()) {
      auto status = object.components.template update<gneiss::camera_description>(
          [](auto& camera) { camera.is_primary = false; });
      if (status.failed())
        return status;
    }
  auto frozen = gneiss::capture_scene_record(*copied);
  if (!frozen)
    return frozen.error();
  snapshot.content = std::move(*frozen);
  std::map<gneiss::guid, scene_object_resources> resources;
  for (const auto& pair : mapping) {
    if (auto item = snapshot.resources.find(pair.source_uuid); item != snapshot.resources.end())
      resources.emplace(pair.target_uuid, item->second);
    if (snapshot.root_uuid == pair.source_uuid)
      snapshot.root_uuid = pair.target_uuid;
  }
  snapshot.resources = std::move(resources);
  snapshot.parent = {target->object, target->source};
  return restore_subtree(snapshot, output);
}

gneiss::result<gneiss::component_descriptions>
scene_edit_session::components(entity_id node) const {
  auto target = locate(node);
  if (!target)
    return target.error();
  return target->effective.components;
}
gneiss::result<prefab_component_state>
scene_edit_session::capture_component_override(entity_id node, gneiss::guid type) {
  {
    auto target = locate(node);
    if (!target)
      return target.error();
    if (!target->source)
      return gneiss::error::unsupported;
    auto description = document_->current().content().description();
    if (!description)
      return description.error();
    auto object = std::ranges::find(description->objects, target->object,
                                    &gneiss::game_object_description::uuid);
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
    if (type == gneiss::mesh_component_description::type_guid) {
      auto binding = capture_mesh_binding(node);
      if (!binding)
        return binding.error();
      result.resources = *binding;
    }
    return result;
  }
}
gneiss::error scene_edit_session::set_component_override(entity_id node, gneiss::guid type,
                                                         const prefab_component_state& state) {
  auto target = locate(node);
  if (!target)
    return target.error();
  if (!target->source)
    return gneiss::error::unsupported;
  if (state.added && !*state.added)
    return gneiss::error::invalid_argument;
  return edit_content([&](auto& description, auto& snapshot) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    auto source = gneiss::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
    if (!source)
      return source.error();
    auto candidate = prepare_prefab_component_override(
        *source, *object->prefab, target->source, type,
        state.added ? *state.added : gneiss::component_description{}, state.removed,
        state.properties, state.component_uuid);
    if (!candidate)
      return candidate.error();
    const auto member = std::ranges::find(candidate->effective.objects, target->source,
                                          &gneiss::game_object_description::uuid);
    if (member == candidate->effective.objects.end())
      return gneiss::error::not_found;
    object->prefab = std::move(candidate->reference);
    if (state.resources.mesh)
      snapshot.assets.insert_or_assign(state.resources.mesh->source().uri, state.resources.mesh);
    if (state.resources.material)
      snapshot.assets.insert_or_assign(state.resources.material->source().uri,
                                       state.resources.material);
    return gneiss::error::success;
  });
}
gneiss::error
scene_edit_session::add_component_description(entity_id node,
                                              const gneiss::component_description& value) {
  return value ? edit_component(node, value->guid(), value, 1)
               : gneiss::error{gneiss::error::invalid_argument};
}
gneiss::error scene_edit_session::remove_component_description(entity_id node, gneiss::guid type) {
  auto policy = editor::component_editor(type);
  if (!policy)
    return policy.error();
  if (!policy->removable)
    return gneiss::error::unsupported;
  return edit_component(node, type, {}, -1);
}
gneiss::error
scene_edit_session::set_component_description(entity_id node,
                                              const gneiss::component_description& value) {
  return value ? edit_component(node, value->guid(), value, 0)
               : gneiss::error{gneiss::error::invalid_argument};
}

gneiss::error
scene_edit_session::set_camera(entity_id node, const gneiss::camera_description& desc) {
  {
    auto current = components(node);
    if (!current)
      return current.error();
    return edit_component(node, gneiss::camera_description::type_guid,
                          gneiss::make_component_description(desc),
                          current->get<gneiss::camera_description>() ? 0 : 1);
  }
}

gneiss::error scene_edit_session::remove_camera(entity_id node) {
  return remove_component_description(node, gneiss::camera_description::type_guid);
}

gneiss::error scene_edit_session::remove_mesh_component(entity_id node) {
  return remove_component_description(node, gneiss::mesh_component_description::type_guid);
}

void scene_edit_session::close() noexcept {
  selection_ = {};
  uri_.clear();
  if (is_open())
    scene_.unload();
  document_.reset();
  projection_ = {};
  scene_ = {};
  application_ = nullptr;
  world_ = 0;
}

gneiss::result<scene_node_record> scene_edit_session::query_node(entity_id node) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->nodes)
    if (item.node == node && node)
      return std::move(item);
  return gneiss::error::not_found;
}
gneiss::result<prefab_node_record> scene_edit_session::query_prefab_node(entity_id node) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.node == node && node)
      return std::move(item);
  return gneiss::error::not_found;
}
gneiss::result<scene_node_record> scene_edit_session::find_node(gneiss::guid uuid) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->nodes)
    if (item.uuid == uuid)
      return std::move(item);
  return gneiss::error::not_found;
}
gneiss::result<prefab_node_record> scene_edit_session::find_prefab_root(gneiss::guid uuid) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.instance_uuid == uuid && item.is_instance_root)
      return std::move(item);
  return gneiss::error::not_found;
}
gneiss::result<prefab_node_record>
scene_edit_session::find_prefab_source(gneiss::guid uuid, gneiss::guid source) const {
  auto snapshot = capture_nodes();
  if (!snapshot)
    return snapshot.error();
  for (auto& item : snapshot->prefab_nodes)
    if (item.instance_uuid == uuid && item.source_node_uuid == source)
      return std::move(item);
  return gneiss::error::not_found;
}
gneiss::result<scene_node_record> scene_edit_session::selected_node() const {
  return query_node(selection_);
}
gneiss::result<prefab_node_record> scene_edit_session::selected_prefab_node() const {
  return query_prefab_node(selection_);
}
gneiss::error scene_edit_session::select(entity_id node) {
  if (!is_open())
    return gneiss::error::invalid_state;
  if (node) {
    auto target = locate(node);
    if (!target)
      return target.error();
  }
  if (node) {
    auto object = world_->object(entity_id{node.get()});
    if (!object)
      return object.error();
  }
  selection_ = node;
  return gneiss::error::success;
}
gneiss::error scene_edit_session::validate_selection() {
  if (!is_open())
    return gneiss::error::invalid_state;
  if (!selection_)
    return gneiss::error::success;
  auto status = select(selection_);
  if (status.failed()) {
    selection_ = {};
    if (status == gneiss::error::invalid_handle || status == gneiss::error::not_found ||
        status == gneiss::error::invalid_argument)
      return gneiss::error::success;
  }
  return status;
}

gneiss::result<scene_mesh_binding> scene_edit_session::capture_mesh_binding(entity_id node) const {
  {
    auto values = components(node);
    if (!values)
      return values.error();
    const auto* renderer = values->get<gneiss::mesh_component_description>();
    if (!renderer)
      return scene_mesh_binding{};
    scene_mesh_binding result;
    const auto& assets = document_->current().content().assets;
    if (auto found = assets.find(renderer->mesh_uri); found != assets.end())
      result.mesh = gneiss::resource_cast<gneiss::mesh>(found->second);
    if (auto found = assets.find(renderer->material_uri); found != assets.end())
      result.material = gneiss::resource_cast<gneiss::material>(found->second);
    return result;
  }
}

gneiss::result<entity_id>
scene_edit_session::create_mesh_node(const mesh_node_creation& snapshot,
                                     const scene_mesh_binding& resources) {
  if (!is_open())
    return gneiss::error::invalid_state;
  if (snapshot.uuid.is_nil())
    return gneiss::error::invalid_argument;
  if (gneiss::resource_uri(resources.mesh) != snapshot.mesh_uri ||
      gneiss::resource_uri(resources.material) != snapshot.material_uri)
    return gneiss::error::invalid_argument;
  auto parent = document_node(*this, snapshot.parent);
  if (!parent)
    return parent.error();
  entity_id created;
  auto status = edit_content(
      [&](auto& description, auto& next) -> gneiss::error {
        gneiss::game_object_description object;
        object.uuid = snapshot.uuid;
        object.parent = snapshot.parent;
        object.name = snapshot.display_name;
        object.components.set(
            gneiss::mesh_component_description{snapshot.mesh_uri, snapshot.material_uri});
        description.objects.push_back(std::move(object));
        retain(next, resources);
        return gneiss::error::success;
      },
      [&] {
        auto result = document_root(*this, snapshot.uuid);
        if (!result)
          return result.error();
        created = *result;
        selection_ = created;
        return gneiss::error{gneiss::error::success};
      });
  if (status.failed())
    return status;
  return created;
}

gneiss::error scene_edit_session::destroy_node(entity_id node) {
  auto target = locate(node);
  if (!target)
    return target.error();
  for (const auto& object : document_->current().content().content.get().objects)
    if (object.parent.object == target->object &&
        (!object.parent.source || object.parent.source == target->source))
      return gneiss::error::invalid_state;
  scene_subtree_snapshot ignored;
  return destroy_subtree(node, ignored);
}

gneiss::error scene_edit_session::set_mesh_component(entity_id node, std::string_view mesh_uri,
                                                     std::string_view material_uri) {
  {
    auto current = components(node);
    if (!current)
      return current.error();
    return edit_component(node, gneiss::mesh_component_description::type_guid,
                          gneiss::make_component_description(gneiss::mesh_component_description{
                              std::string(mesh_uri), std::string(material_uri)}),
                          current->get<gneiss::mesh_component_description>() ? 0 : 1);
  }
}

gneiss::error scene_edit_session::replace_mesh_binding(entity_id node,
                                                       const scene_mesh_binding& expected,
                                                       const scene_mesh_binding& replacement) {
  auto current = capture_mesh_binding(node);
  if (!current)
    return current.error();
  if (current->mesh != expected.mesh || current->material != expected.material)
    return gneiss::error::invalid_state;
  auto target = locate(node);
  if (!target)
    return target.error();
  if (target->source)
    return gneiss::error::unsupported;
  return edit_content([&](auto& description, auto& next) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    object->components.set(gneiss::mesh_component_description{
        std::string(gneiss::resource_uri(replacement.mesh)),
        std::string(gneiss::resource_uri(replacement.material))});
    retain(next, replacement);
    return gneiss::error::success;
  });
}

gneiss::result<std::string> scene_edit_session::serialize() const {
  std::string output;
  auto status = encode_scene(output);
  return status.ok() ? gneiss::result<std::string>{std::move(output)}
                     : gneiss::result<std::string>{status};
}

gneiss::error scene_edit_session::encode_scene(std::string& output) const {
  auto snapshot = capture_content();
  if (!snapshot)
    return snapshot.error();
  auto description = snapshot->description();
  if (!description)
    return description.error();
  auto encoded = gneiss::asset_file::serialize_bytes(*description);
  if (!encoded)
    return encoded.error().error;
  output.assign(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  return gneiss::error::success;
}

gneiss::error scene_edit_session::begin_save(gneiss::files::service& files,
                                             std::string_view uri,
                                             gneiss::files::write_mode mode,
                                             std::shared_ptr<save_receipt>& output) {
  if (edit_base_)
    return gneiss::error::invalid_state;
  output.reset();
  if (!is_open() || !uri.starts_with("asset://") || gneiss::validate_uri(uri).failed())
    return gneiss::error::invalid_argument;
  {
    auto receipt = std::make_shared<save_receipt>();
    receipt->scene = scene_;
    receipt->version = document_->current();
    receipt->uri = uri;
    auto status = encode_scene(receipt->content);
    if (status.failed())
      return status;
    const auto bytes = std::as_bytes(std::span{receipt->content.data(), receipt->content.size()});
    gneiss::files::request_id request;
    status = files.write(
        uri, {bytes.begin(), bytes.end()}, {mode},
        [receipt](gneiss::error value) {
          receipt->status = value;
          receipt->complete = true;
        },
        request);
    if (status.ok())
      output = std::move(receipt);
    return status;
  }
}
gneiss::error scene_edit_session::accept_save(const save_receipt& receipt) {
  if (edit_base_)
    return gneiss::error::invalid_state;
  if (!receipt.complete)
    return gneiss::error::not_ready;
  if (receipt.status.failed())
    return receipt.status;
  if (!is_open() || scene_ != receipt.scene)
    return gneiss::error::invalid_state;
  {
    auto saved = document_->mark_saved(receipt.version);
    if (saved.failed())
      return saved;
    uri_ = receipt.uri;
    return gneiss::error::success;
  }
}

gneiss::error scene_edit_session::restore_changes(const scene_document::revision& expected,
                                                  const scene_document::revision& target,
                                                  std::span<const document_patch> changes,
                                                  bool undo) {
  if (!is_open() || document_->current().stamp() != expected)
    return gneiss::error::invalid_state;
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
  status =
      project_scene_document(scene_, projection_, synchronized ? &before.content() : nullptr, next);
  (void)report_projection(document_->current(), status);
  (void)validate_selection();
  return gneiss::error::success;
}

gneiss::error scene_edit_session::restore_version(const version& expected, const version& target) {
  if (!is_open())
    return gneiss::error::invalid_state;
  const bool synchronized = projection_.is_synchronized(document_->current());
  auto status = document_->checkout(expected, target);
  if (status.failed())
    return status;
  if (synchronized) {
    status = project_scene_document(scene_, projection_, &expected.content(), target.content());
    (void)report_projection(target, status);
    (void)validate_selection();
  } else {
    (void)synchronize();
  }
  return gneiss::error::success;
}

gneiss::error scene_edit_session::edit_component(entity_id node, gneiss::guid type,
                                                 const gneiss::component_description& value,
                                                 int mode) {
  auto target = locate(node);
  if (!target)
    return target.error();
  const auto* current = target->effective.components.find(type);
  if (mode == 1 && current)
    return gneiss::error::invalid_state;
  if (mode != 1 && !current)
    return gneiss::error::not_found;
  gneiss::guid identity;
  return edit_content([&](auto& description, auto& snapshot) -> gneiss::error {
    auto object = std::ranges::find(description.objects, target->object,
                                    &gneiss::game_object_description::uuid);
    if (mode == 1)
      identity = gneiss::guid::generate();
    if (target->source) {
      auto source = gneiss::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
      if (!source)
        return source.error();
      auto local = std::ranges::find_if(object->prefab->component_overrides, [&](const auto& item) {
        return item.source_node_uuid == target->source && item.type_guid == type &&
               item.added.has_value();
      });
      auto candidate =
          mode == 0
              ? prepare_prefab_component_edit(*source, *object->prefab, target->source, *current,
                                              value)
              : prepare_prefab_component_override(
                    *source, *object->prefab, target->source, type,
                    mode == 1 ? value : gneiss::component_description{},
                    mode == -1 && local == object->prefab->component_overrides.end(), {}, identity);
      if (!candidate)
        return candidate.error();
      object->prefab = std::move(candidate->reference);
    } else {
      if (mode == -1)
        object->components.erase(type);
      else if (mode == 1) {
        auto entries = object->components.entries();
        auto identified = gneiss::with_component_identity(value, identity);
        if (!identified)
          return identified.error();
        entries.push_back(std::move(*identified));
        auto status = object->components.replace(std::move(entries));
        if (status.failed())
          return status;
      } else
        object->components.set(value);
      if (value && type == gneiss::camera_description::type_guid &&
          value->get<gneiss::camera_description>()->is_primary) {
        for (auto& other : description.objects) {
          if (other.uuid == object->uuid)
            continue;
          if (other.components.template get<gneiss::camera_description>()) {
            auto status = other.components.template update<gneiss::camera_description>(
                [](auto& camera) { camera.is_primary = false; });
            if (status.failed())
              return status;
          }
        }
      }
    }
    return gneiss::error::success;
  });
}

gneiss::result<scene_subtree_snapshot> scene_edit_session::capture_subtree(entity_id node) const {
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
      return gneiss::error::unsupported;
  }
  scene_subtree_snapshot result;
  result.root_uuid = target->object;
  std::set<gneiss::guid> included{target->object};
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto& object : description->objects)
      if (included.contains(object.parent.object))
        changed |= included.insert(object.uuid).second;
  }
  std::erase_if(description->objects,
                [&](const auto& object) { return !included.contains(object.uuid); });
  for (auto& object : description->objects) {
    if (object.uuid == result.root_uuid) {
      result.parent = object.parent;
      object.parent = {};
    }
    if (!object.prefab && !object.components.template get<gneiss::mesh_component_description>())
      continue;
    auto& resources = result.resources[object.uuid];
    if (object.prefab) {
      const auto& frozen = snapshot->prefabs.at(object.prefab->uri);
      resources.prefab_content = frozen.content;
      resources.source_asset = frozen.source_asset;
      auto source = gneiss::restore_prefab_record(frozen.content);
      if (!source)
        return source.error();
      auto effective = gneiss::replay_prefab_reference(*source, *object.prefab);
      if (!effective)
        return effective.error();
      for (const auto& member : effective->description.objects)
        resources.source_nodes.push_back(binding_for(member.components, *snapshot));
    } else
      resources.renderer = binding_for(object.components, *snapshot);
  }
  auto content = gneiss::capture_scene_record(*description);
  if (!content)
    return content.error();
  result.content = std::move(*content);
  return result;
}
gneiss::result<prefab_header_snapshot>
scene_edit_session::capture_prefab_header(gneiss::guid uuid) const {
  auto snapshot = capture_content();
  if (!snapshot)
    return snapshot.error();
  auto description = snapshot->description();
  if (!description)
    return description.error();
  auto object =
      std::ranges::find(description->objects, uuid, &gneiss::game_object_description::uuid);
  if (object == description->objects.end() || !object->prefab)
    return gneiss::error::not_found;
  const auto& frozen = snapshot->prefabs.at(object->prefab->uri);
  auto source = gneiss::restore_prefab_record(frozen.content);
  if (!source)
    return source.error();
  return prefab_header_snapshot{
      *object, gneiss::make_ref<const gneiss::prefab_description>(std::move(*source)),
      frozen.source_asset};
}
gneiss::error scene_edit_session::apply_prefab(const scene_subtree_snapshot& snapshot,
                                               bool removing, bool replacing) {
  return edit_content(
      [&](auto& description, auto& next) -> gneiss::error {
        if (!removing)
          return import_subtree(snapshot, description, next, replacing);
        if (replacing)
          return gneiss::error::invalid_argument;
        std::erase_if(description.objects, [&](const auto& object) {
          return std::ranges::find(snapshot.content.get().objects, object.uuid,
                                   &gneiss::object_record::uuid) !=
                 snapshot.content.get().objects.end();
        });
        return gneiss::error::success;
      },
      [&] {
        if (removing) {
          (void)validate_selection();
          return gneiss::error{gneiss::error::success};
        }
        auto root = document_root(*this, snapshot.root_uuid);
        if (!root)
          return root.error();
        selection_ = *root;
        return gneiss::error{gneiss::error::success};
      });
}
} // namespace gneiss::editor
