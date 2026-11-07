// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/scene_document.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/prefab_placement.hpp"

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

bool scene_document::is_open() const noexcept {
  if (!application_ || !scene_ || !application_->is_owner_thread() ||
      !application_->accepts_requests() || application_->native_world() != world_)
    return false;
  // 地址可能被后续 World 复用；实例 ID 的所属域仍必须匹配当前实例集合。
  auto* scenes = application_->scenes();
  return scenes && versions_ && bool(versions_->current());
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
    versions_ = std::move(other.versions_);
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
    auto captured = application.scenes()->capture_prepared(scene);
    auto initial = captured ? make_scene_document_snapshot(std::string(uri), captured->description)
                            : engine::result<scene_document_snapshot>{captured.error()};
    pending.versions_ = std::make_unique<scene_document_versions>(0);
    if (initial)
      initial->assets = std::move(captured->assets);
    const auto refreshed = initial ? pending.versions_->reset(*initial) : initial.error();
    if (refreshed.ok())
      (void)pending.versions_->report_projection(pending.versions_->current(),
                                                 engine::error::success);
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
  engine::scene_instantiation_data initial;
  initial.description.uuid = scene_uuid;
  auto frozen = make_scene_document_snapshot({}, initial);
  if (!frozen) {
    (void)application.scenes()->unload(*created);
    return frozen.error();
  }
  std::unique_ptr<scene_document_versions> versions;
  try {
    versions = std::make_unique<scene_document_versions>(0);
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
  versions_ = std::move(versions);
  (void)versions_->report_projection(versions_->current(), engine::error::success);
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

engine::result<scene_document_snapshot> scene_document::capture_content() const noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  auto snapshot = versions_->current().content();
  snapshot.uri = uri_;
  return snapshot;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_document::projection_status() const noexcept {
  return versions_ ? versions_->projection_status() : engine::error{engine::error::invalid_state};
}
engine::error scene_document::accept_content(const scene_document_snapshot& snapshot) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  if (snapshot.uri != uri_)
    return engine::error::invalid_argument;
  auto status = versions_->accept(versions_->current(), snapshot, {});
  if (status.failed())
    return status;
  is_dirty_ = true;
  (void)synchronize();
  return engine::error::success;
}
engine::error scene_document::synchronize() noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  const auto version = versions_->current();
  auto selected = locate(selection_);
  auto inputs = make_scene_document_inputs(version.content());
  if (!inputs) {
    (void)versions_->report_projection(version, inputs.error());
    return inputs.error();
  }
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (status.ok()) {
    status = application_->scenes()->replace_prepared(scene_, std::move(prepared),
                                                      version.content().assets);
    if (status.ok()) {
      selection_ = {};
      if (selected) {
        if (selected->source) {
          auto node = find_prefab_source(selected->object, selected->source);
          if (node) selection_ = node->node;
        } else {
          auto node = find_node(selected->object);
          if (node) selection_ = node->node;
        }
      }
    }
  }
  (void)versions_->report_projection(version, status);
  return status;
} catch (const std::bad_alloc&) {
  if (versions_)
    (void)versions_->report_projection(versions_->current(), engine::error::out_of_memory);
  return engine::error::out_of_memory;
} catch (...) {
  if (versions_)
    (void)versions_->report_projection(versions_->current(), engine::error::internal);
  return engine::error::internal;
}
engine::error scene_document::edit_content(const content_edit& edit,
                                           const std::function<engine::error()>& project) noexcept
    try {
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
  // 同 URI 的冻结来源由所有实例共享；单实例投影不足以声明整份新来源已同步。
  const bool changed_shared_source =
      std::ranges::any_of(prepared->snapshot.prefabs, [&](const auto& entry) {
        const auto previous = versions_->current().content().prefabs.find(entry.first);
        return previous != versions_->current().content().prefabs.end() &&
               previous->second != entry.second &&
               std::ranges::count_if(description->objects, [&](const auto& object) {
                 return object.prefab && object.prefab->uri == entry.first;
               }) > 1;
      });
  const bool synchronized = versions_->is_synchronized();
  const auto previous_projection = versions_->projection_status();
  status = versions_->accept(versions_->current(), prepared->snapshot, {});
  if (status.failed())
    return status;
  is_dirty_ = true;
  auto projection = engine::error{engine::error::internal};
  try {
    projection = project();
  } catch (const std::bad_alloc&) {
    projection = engine::error::out_of_memory;
  } catch (...) {
    projection = engine::error::internal;
  }
  if (projection.ok() && changed_shared_source)
    projection = engine::error::not_ready;
  // 增量成功不能掩盖之前的失败；完整 synchronize 才清除未同步状态。
  (void)versions_->report_projection(
      versions_->current(), synchronized || projection.failed() ? projection : previous_projection);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_document::edit_target>
scene_document::locate(scene_node_id node) const noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  if (!node)
    return engine::error::invalid_handle;
  auto info = query_info([&](const auto& info) { return info.node == node; });
  if (!info)
    return info.error();
  auto description = versions_->current().content().description();
  if (!description)
    return description.error();
  const auto uuid = info->prefab ? info->prefab->instance_uuid : info->uuid;
  auto object =
      std::ranges::find(description->objects, uuid, &engine::game_object_description::uuid);
  if (object == description->objects.end())
    return engine::error::not_found;
  if (!object->prefab)
    return edit_target{uuid, {}, *object};
  auto source = versions_->current().content().prefabs.find(object->prefab->uri);
  if (source == versions_->current().content().prefabs.end())
    return engine::error::not_found;
  auto restored = engine::restore_prefab_record(source->second.content);
  if (!restored)
    return restored.error();
  auto effective = engine::replay_prefab_reference(*restored, *object->prefab);
  if (!effective)
    return effective.error();
  const auto source_uuid = info->prefab->source_node_uuid;
  auto member = std::ranges::find(effective->description.objects, source_uuid,
                                  &engine::game_object_description::uuid);
  if (member == effective->description.objects.end())
    return engine::error::not_found;
  if (source_uuid == restored->root_uuid) {
    member->transform = object->transform;
    member->name = object->name;
  }
  return edit_target{uuid, source_uuid, *member};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
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
        return scenes.get_node_info(scene_, index, engine::node_query_detail::identity);
    }
    return engine::error::not_found;
  });
}
engine::result<scene_query_snapshot> scene_document::capture_nodes() const noexcept {
  try {
    auto snapshot = capture_content();
    if (!snapshot)
      return snapshot.error();
    auto description = snapshot->description();
    if (!description)
      return description.error();
    using key = std::pair<engine::guid, engine::guid>;
    std::map<key, engine::scene_node_info> projected;
    auto count = application_->scenes()->get_node_count(scene_);
    if (count)
      for (std::uint64_t i = 0; i < *count; ++i) {
        auto info =
            application_->scenes()->get_node_info(scene_, i, engine::node_query_detail::identity);
        if (!info)
          continue;
        key identity = info->prefab
                           ? key{info->prefab->instance_uuid, info->prefab->source_node_uuid}
                           : key{info->uuid, {}};
        projected.emplace(identity, *info);
      }
    const auto explicit_node = [&](engine::guid uuid) -> scene_node_id {
      if (!uuid)
        return {};
      auto object =
          std::ranges::find(description->objects, uuid, &engine::game_object_description::uuid);
      if (object == description->objects.end())
        return {};
      engine::guid source;
      if (object->prefab)
        source = snapshot->prefabs.at(object->prefab->uri).content.get().root_uuid;
      auto found = projected.find({uuid, source});
      return found == projected.end() ? scene_node_id{} : found->second.node;
    };
    scene_query_snapshot result;
    for (const auto& object : description->objects) {
      if (!object.prefab) {
        auto found = projected.find({object.uuid, {}});
        result.nodes.push_back(
            {.node = explicit_node(object.uuid),
             .parent = explicit_node(object.parent_uuid),
             .entity = found == projected.end() ? entity_id{} : found->second.entity,
             .uuid = object.uuid,
             .display_name = object.name.empty() ? object.uuid.string() : object.name,
             .local_transform = object.transform,
             .components = object.components});
        continue;
      }
      auto source = engine::restore_prefab_record(snapshot->prefabs.at(object.prefab->uri).content);
      if (!source)
        return source.error();
      auto effective = engine::replay_prefab_reference(*source, *object.prefab);
      if (!effective)
        return effective.error();
      for (const auto& member : effective->description.objects) {
        const bool root = member.uuid == source->root_uuid;
        auto found = projected.find({object.uuid, member.uuid});
        auto parent = projected.find({object.uuid, member.parent_uuid});
        auto original =
            std::ranges::find(source->objects, member.uuid, &engine::game_object_description::uuid);
        prefab_node_record record;
        record.node = found == projected.end() ? scene_node_id{} : found->second.node;
        record.parent = root                        ? explicit_node(object.parent_uuid)
                        : parent == projected.end() ? scene_node_id{}
                                                    : parent->second.node;
        record.entity = found == projected.end() ? entity_id{} : found->second.entity;
        record.instance_uuid = object.uuid;
        record.source_node_uuid = member.uuid;
        record.display_name = root ? object.name : member.name;
        if (record.display_name.empty())
          record.display_name = (root ? object.uuid : member.uuid).string();
        record.prefab_uri = object.prefab->uri;
        record.local_transform = root ? object.transform : member.transform;
        record.source_local_transform = original->transform;
        record.is_instance_root = root;
        for (const auto& override : object.prefab->overrides)
          if (override.key.target.source_node_uuid == member.uuid)
            record.overrides.push_back(override.key);
        result.prefab_nodes.push_back(std::move(record));
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

gneiss::engine::error
scene_document::get_world_transform(scene_node_id node,
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
scene_document::set_local_transform(scene_node_id node,
                                    const gneiss::engine::math::transform& value) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content(
      [&](auto& description, auto& snapshot) -> engine::error {
        auto object = std::ranges::find(description.objects, target->object,
                                        &engine::game_object_description::uuid);
        if (!target->source ||
            target->source == snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid) {
          object->transform = value;
        } else {
          auto source =
              engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
          if (!source)
            return source.error();
          auto candidate = engine::prepare_prefab_transform_edit(
              *source, *object->prefab, target->source, target->effective.transform, value);
          if (!candidate)
            return candidate.error();
          object->prefab = std::move(candidate->reference);
        }
        return engine::error::success;
      },
      [&] {
        if (target->source && target->source != versions_->current()
                                                    .content()
                                                    .prefabs.at(query_prefab_node(node)->prefab_uri)
                                                    .content.get()
                                                    .root_uuid)
          return application_->scenes()->set_prefab_source_transform(scene_, node, value);
        return world_->node_set_local_transform(node, value);
      });
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
        auto created = application_->scenes()->create_node(
            scene_, {.uuid = uuid, .name = name, .parent = parent});
        if (!created)
          return created.error();
        out_node = *created;
        selection_ = out_node;
        return engine::error{engine::error::success};
      });
}

gneiss::engine::error scene_document::rename_node(scene_node_id node,
                                                  std::string_view name) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return edit_content(
      [&](auto& description, auto& snapshot) -> engine::error {
        auto object = std::ranges::find(description.objects, target->object,
                                        &engine::game_object_description::uuid);
        if (target->source &&
            target->source != snapshot.prefabs.at(object->prefab->uri).content.get().root_uuid)
          return engine::error::unsupported;
        object->name = name;
        return engine::error::success;
      },
      [&] { return application_->scenes()->set_node_name(scene_, node, name); });
}

gneiss::engine::error scene_document::reparent_node(scene_node_id node,
                                                    scene_node_id parent) noexcept {
  auto target = locate(node);
  auto destination = parent ? locate(parent) : engine::result<edit_target>{edit_target{}};
  if (!target)
    return target.error();
  if (!destination)
    return destination.error();
  return edit_content(
      [&](auto& description, auto& snapshot) -> engine::error {
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
      },
      [&] { return application_->scenes()->reparent_node(scene_, node, parent); });
}

engine::error scene_document::destroy_subtree(scene_node_id node,
                                              scene_subtree_snapshot& output) noexcept try {
  auto snapshot = capture_subtree(node);
  if (!snapshot)
    return snapshot.error();
  const auto result = edit_content(
      [&](auto& description, auto&) -> engine::error {
        std::erase_if(description.objects, [&](const auto& object) {
          return std::ranges::find(snapshot->content.get().objects, object.uuid,
                                   &engine::object_record::uuid) !=
                 snapshot->content.get().objects.end();
        });
        return engine::error::success;
      },
      [&] { return application_->scenes()->destroy_subtree(scene_, node); });
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

engine::error scene_document::restore_subtree(const scene_subtree_snapshot& snapshot,
                                              scene_node_id& output) noexcept {
  output = {};
  scene_node_id parent;
  if (snapshot.parent_uuid) {
    auto ordinary = find_node(snapshot.parent_uuid);
    auto prefab = find_prefab_root(snapshot.parent_uuid);
    if (!ordinary && !prefab)
      return engine::error::not_found;
    parent = ordinary ? ordinary->node : prefab->node;
  }
  return edit_content(
      [&](auto& description, auto& next) {
        return import_subtree(snapshot, description, next, false);
      },
      [&] {
        auto created = application_->scenes()->restore_subtree(scene_, snapshot, parent, {});
        if (!created)
          return created.error();
        output = *created;
        selection_ = output;
        return engine::error{engine::error::success};
      });
}

engine::error scene_document::duplicate_subtree(scene_node_id node, scene_node_id parent,
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
      snapshot.content, versions_->current().content().content.get().uuid, mapping);
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
scene_document::components(scene_node_id node) const noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  return target->effective.components;
}
engine::result<engine::prefab_component_state>
scene_document::capture_component_override(scene_node_id node, engine::guid type) noexcept {
  try {
    auto target = locate(node);
    if (!target)
      return target.error();
    if (!target->source)
      return engine::error::unsupported;
    auto description = versions_->current().content().description();
    if (!description)
      return description.error();
    auto object = std::ranges::find(description->objects, target->object,
                                    &engine::game_object_description::uuid);
    engine::prefab_component_state result;
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
scene_document::set_component_override(scene_node_id node, engine::guid type,
                                       const engine::prefab_component_state& state) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  if (!target->source)
    return engine::error::unsupported;
  if (state.added && !*state.added)
    return engine::error::invalid_argument;
  auto projected = state;
  return edit_content(
      [&](auto& description, auto& snapshot) -> engine::error {
        auto object = std::ranges::find(description.objects, target->object,
                                        &engine::game_object_description::uuid);
        auto source =
            engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
        if (!source)
          return source.error();
        auto candidate = engine::prepare_prefab_component_override(
            *source, *object->prefab, target->source, type,
            state.added ? *state.added : engine::component_description{}, state.removed,
            state.properties, state.component_uuid);
        if (!candidate)
          return candidate.error();
        for (const auto& entry : candidate->reference.component_overrides)
          if (entry.source_node_uuid == target->source && entry.type_guid == type)
            projected.component_uuid = entry.component_uuid;
        object->prefab = std::move(candidate->reference);
        if (state.resources.mesh)
          snapshot.assets.insert_or_assign(state.resources.mesh->source().uri,
                                           state.resources.mesh);
        if (state.resources.material)
          snapshot.assets.insert_or_assign(state.resources.material->source().uri,
                                           state.resources.material);
        return engine::error::success;
      },
      [&] {
        return application_->scenes()->set_component_override(scene_, node, type, projected);
      });
}
engine::error
scene_document::add_component_description(scene_node_id node,
                                          const engine::component_description& value) noexcept {
  return value ? edit_component(node, value->guid(), value, 1)
               : engine::error{engine::error::invalid_argument};
}
engine::error scene_document::remove_component_description(scene_node_id node,
                                                           engine::guid type) noexcept {
  return edit_component(node, type, {}, -1);
}
engine::error
scene_document::set_component_description(scene_node_id node,
                                          const engine::component_description& value) noexcept {
  return value ? edit_component(node, value->guid(), value, 0)
               : engine::error{engine::error::invalid_argument};
}

gneiss::engine::error
scene_document::set_camera(scene_node_id node,
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

gneiss::engine::error scene_document::remove_camera(scene_node_id node) noexcept {
  return remove_component_description(node, engine::camera_description::type_guid);
}

gneiss::engine::error scene_document::remove_mesh_renderer(scene_node_id node) noexcept {
  return remove_component_description(node, engine::mesh_renderer_description::type_guid);
}

void scene_document::close() noexcept {
  selection_ = {};
  uri_.clear();
  is_dirty_ = false;
  if (is_open())
    (void)with_scene([&](auto& service) { return service.unload(scene_); });
  versions_.reset();
  scene_ = {};
  application_ = nullptr;
  world_ = 0;
}

engine::result<scene_node_record> scene_document::query_node(scene_node_id node) const noexcept
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
scene_document::query_prefab_node(scene_node_id node) const noexcept try {
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
engine::result<scene_node_record> scene_document::find_node(engine::guid uuid) const noexcept try {
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
scene_document::find_prefab_root(engine::guid uuid) const noexcept try {
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
scene_document::find_prefab_source(engine::guid uuid, engine::guid source) const noexcept try {
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
  try {
    auto values = components(node);
    if (!values)
      return values.error();
    const auto* renderer = values->get<engine::mesh_renderer_description>();
    if (!renderer)
      return engine::scene_mesh_binding{};
    engine::scene_mesh_binding result;
    const auto& assets = versions_->current().content().assets;
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
scene_document::create_mesh_node(const mesh_node_creation& snapshot,
                                 const engine::scene_mesh_binding& resources) noexcept try {
  if (!is_open())
    return engine::error::invalid_state;
  if (snapshot.uuid.is_nil())
    return engine::error::invalid_argument;
  if (engine::resource_uri(resources.mesh) != snapshot.mesh_uri ||
      engine::resource_uri(resources.material) != snapshot.material_uri)
    return engine::error::invalid_argument;
  scene_node_id parent;
  if (snapshot.parent_uuid) {
    auto found = find_node(snapshot.parent_uuid);
    if (!found)
      return found.error();
    parent = found->node;
  }
  scene_node_id created;
  engine::guid component_uuid;
  auto status = edit_content(
      [&](auto& description, auto& next) -> engine::error {
        engine::game_object_description object;
        object.uuid = snapshot.uuid;
        object.parent_uuid = snapshot.parent_uuid;
        object.name = snapshot.display_name;
        object.components.set(
            engine::mesh_renderer_description{snapshot.mesh_uri, snapshot.material_uri});
        component_uuid = object.components.identity(engine::mesh_renderer_description::type_guid);
        description.objects.push_back(std::move(object));
        retain(next, resources);
        return engine::error::success;
      },
      [&] {
        auto result = application_->scenes()->create_mesh_renderer_node_ready(
            scene_,
            {.uuid = snapshot.uuid,
             .name = snapshot.display_name,
             .parent = parent,
             .mesh_uri = snapshot.mesh_uri,
             .material_uri = snapshot.material_uri,
             .component_uuid = component_uuid},
            resources);
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

gneiss::engine::error scene_document::destroy_node(scene_node_id node) noexcept {
  auto target = locate(node);
  if (!target)
    return target.error();
  for (const auto& object : versions_->current().content().content.get().objects)
    if (object.parent == target->object)
      return engine::error::invalid_state;
  scene_subtree_snapshot ignored;
  return destroy_subtree(node, ignored);
}

gneiss::engine::error scene_document::set_mesh_renderer(scene_node_id node,
                                                        std::string_view mesh_uri,
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
scene_document::replace_mesh_binding(scene_node_id node, const engine::scene_mesh_binding& expected,
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
  engine::guid component_uuid;
  return edit_content(
      [&](auto& description, auto& next) -> engine::error {
        auto object = std::ranges::find(description.objects, target->object,
                                        &engine::game_object_description::uuid);
        object->components.set(engine::mesh_renderer_description{
            std::string(engine::resource_uri(replacement.mesh)),
            std::string(engine::resource_uri(replacement.material))});
        component_uuid = object->components.identity(engine::mesh_renderer_description::type_guid);
        retain(next, replacement);
        return engine::error::success;
      },
      [&] {
        return application_->scenes()->replace_mesh_binding(scene_, node, expected, replacement,
                                                            component_uuid);
      });
}

engine::result<std::string> scene_document::serialize() const noexcept {
  std::string output;
  auto status = encode_scene(output);
  return status.ok() ? engine::result<std::string>{std::move(output)}
                     : engine::result<std::string>{status};
}

engine::error scene_document::encode_scene(std::string& output) const noexcept try {
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
    receipt->version = versions_->current();
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
    auto saved = versions_->mark_saved(receipt.version);
    if (saved.failed())
      return saved;
    uri_ = receipt.uri;
    is_dirty_ = current != receipt.content;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_document::restore_version(const version& expected,
                                              const version& target) noexcept {
  if (!is_open())
    return engine::error::invalid_state;
  auto status = versions_->checkout(expected, target);
  if (status.failed())
    return status;
  is_dirty_ = versions_->is_dirty();
  (void)synchronize();
  return engine::error::success;
}

engine::error scene_document::edit_component(scene_node_id node, engine::guid type,
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
  return edit_content(
      [&](auto& description, auto& snapshot) -> engine::error {
        auto object = std::ranges::find(description.objects, target->object,
                                        &engine::game_object_description::uuid);
        if (mode == 1)
          identity = engine::guid::generate();
        if (target->source) {
          auto source =
              engine::restore_prefab_record(snapshot.prefabs.at(object->prefab->uri).content);
          if (!source)
            return source.error();
          auto local =
              std::ranges::find_if(object->prefab->component_overrides, [&](const auto& item) {
                return item.source_node_uuid == target->source && item.type_guid == type &&
                       item.added.has_value();
              });
          auto candidate =
              mode == 0 ? engine::prepare_prefab_component_edit(*source, *object->prefab,
                                                                target->source, *current, value)
                        : engine::prepare_prefab_component_override(
                              *source, *object->prefab, target->source, type,
                              mode == 1 ? value : engine::component_description{},
                              mode == -1 && local == object->prefab->component_overrides.end(), {},
                              identity);
          if (!candidate)
            return candidate.error();
          object->prefab = std::move(candidate->reference);
        } else {
          if (mode == -1)
            object->components.erase(type);
          else if (mode == 1) {
            auto entries = object->components.entries();
            entries.push_back({identity, value});
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
      },
      [&] {
        if (mode == 1)
          return application_->scenes()->add_component_description(scene_, node, value, identity);
        if (mode == -1)
          return application_->scenes()->remove_component_description(scene_, node, type);
        return application_->scenes()->set_component_description(scene_, node, value);
      });
}

engine::result<scene_subtree_snapshot>
scene_document::capture_subtree(scene_node_id node) const noexcept try {
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
engine::result<engine::prefab_header_snapshot>
scene_document::capture_prefab_header(engine::guid uuid) const noexcept try {
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
  return engine::prefab_header_snapshot{
      *object, engine::make_ref<const engine::prefab_description>(std::move(*source)),
      frozen.source_asset};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_document::apply_prefab(const scene_subtree_snapshot& snapshot, bool removing,
                                           bool replacing) noexcept {
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
        scene_node_id root;
        auto status = runtime::prefab_placement::apply(*application_, scene_, snapshot, removing,
                                                       replacing, root);
        if (status.ok() && root)
          selection_ = root;
        return status;
      });
}
} // namespace gneiss::editor
