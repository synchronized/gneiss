// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/commands/editor_asset_edit.hpp"
#include "documents/commands/prefab_authoring.hpp"
#include "documents/prefab_edit.hpp"
#include "documents/scene_document_edit.hpp"
#include <algorithm>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/meta/resolve_path.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/prefab_property_override.hpp>

namespace gneiss::editor {
namespace {
entity_id locate(scene_edit_session& document, const component_edit_target& target) {
  if (target.instance_uuid) {
    const auto node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    return node ? node->node : entity_id{};
  }
  const auto node = document.find_node(target.object_uuid);
  return node ? node->node : entity_id{};
}
}

gneiss::error editor_asset_edit::commit_add(scene_edit_session& session,
                                                    editor_command_history& history) {
  return execute_scene_edit(history, session, "创建 Mesh Renderer 节点", [&] {
    if (session.scene_handle() != scene_)
      return gneiss::error{gneiss::error::invalid_state};
    auto created = session.create_mesh_node(snapshot_, next_);
    return created ? gneiss::error{gneiss::error::success} : created.error();
  });
}

gneiss::error editor_asset_edit::commit(runtime::application& application,
                                                scene_edit_session& session,
                                                editor_command_history& history) {
  if (!application.is_owner_thread() || session.scene_handle() != scene_) {
    return gneiss::error::invalid_state;
  }
  if (creating_) {
    return commit_add(session, history);
  }
  if (locate(session, target_) != node_)
    return gneiss::error::invalid_state;
  // 重复设置相同资源不创建历史；来源恢复走文档操作入口。
  if (previous_.mesh == next_.mesh && previous_.material == next_.material)
    return gneiss::error::success;
  if (prefab_before_) {
    auto captured = session.capture_mesh_binding(node_);
    if (!captured)
      return captured.error();
    const auto& current = *captured;
    if (current.mesh != previous_.mesh || current.material != previous_.material)
      return gneiss::error::invalid_state;
    prefab_component_state after;
    after.resources = next_;
    {
      auto status_value = session.capture_prefab_header(target_.instance_uuid);
      if (!status_value)
        return status_value.error();
      const auto& source = status_value->source;
      auto object = std::ranges::find(source->objects, target_.source_uuid,
                                      &gneiss::game_object_description::uuid);
      if (object == source->objects.end())
        return gneiss::error::not_found;
      const auto* original = object->components.get<gneiss::mesh_component_description>();
      gneiss::mesh_component_description description{mesh_, material_};
      if (!original) {
        after.added = gneiss::make_component_description(description);
        after.component_uuid = prefab_before_->component_uuid ? prefab_before_->component_uuid
                                                              : gneiss::guid::generate();
      } else {
        auto overrides = prepare_mesh_component_overrides(
            target_.source_uuid,
            object->components.identity(gneiss::mesh_component_description::type_guid), *original,
            description);
        if (!overrides)
          return overrides.error();
        after.properties = std::move(*overrides);
      }
    }
    auto apply = [&session, scene = scene_, target = target_](const prefab_component_state& value) {
      if (session.scene_handle() != scene)
        return gneiss::error{gneiss::error::invalid_state};
      auto node = locate(session, target);
      return node ? session.set_component_override(
                        node, gneiss::mesh_component_description::type_guid, value)
                  : gneiss::error{gneiss::error::not_found};
    };
    return execute_scene_edit(history, session, "修改实例网格组件", [&] { return apply(after); });
  }

  const auto apply = [&session, scene = scene_, target = target_](
                         const auto& expected, const auto& replacement) -> gneiss::error {
    if (session.scene_handle() != scene) {
      return gneiss::error::invalid_state;
    }
    const auto node = locate(session, target);
    if (!node)
      return gneiss::error::not_found;
    return session.replace_mesh_binding(node, expected, replacement);
  };
  const auto was_dirty = session.is_dirty();
  const auto operation = execute_scene_edit(history, session, "替换 Mesh Renderer 资源",
                                            [&] { return apply(previous_, next_); });
  if (operation.failed() && !was_dirty) {
    session.clear_dirty();
  }
  return operation;
}

gneiss::error editor_asset_edit::request_remove(scene_edit_session& session,
                                                editor_command_history& history,
                                                gneiss::guid uuid) {
  if (active_ || history.is_pending())
    return gneiss::error::not_ready;
  const auto node = session.find_node(uuid);
  if (!node)
    return gneiss::error::not_found;
  if (!node->components.get<gneiss::mesh_component_description>())
    return gneiss::error::not_found;
  const auto was_dirty = session.is_dirty();
  auto status = execute_scene_edit(history, session, "移除 Mesh Renderer",
                                   [&] { return session.remove_mesh_component(node->node); });
  if (status.failed() && !was_dirty)
    session.clear_dirty();
  return status;
}

gneiss::error editor_asset_edit::request(runtime::application& application,
                                                 scene_edit_session& session,
                                                 editor_command_history& history,
                                                 gneiss::guid uuid,
                                                 mesh_asset_reference assets) {
  return request_impl(application, session, history, {.object_uuid = uuid}, {}, assets, false);
}
gneiss::error editor_asset_edit::request(runtime::application& app, scene_edit_session& document,
                                         editor_command_history& history,
                                         const component_edit_target& target,
                                         mesh_asset_reference assets) {
  return request_impl(app, document, history, target, {}, assets, false);
}
gneiss::error editor_asset_edit::request_property(runtime::application& app,
                                                  scene_edit_session& document,
                                                  editor_command_history& history,
                                                  const component_edit_target& target,
                                                  const property_edit& edit) {
  using namespace gneiss;
  if (active_ || history.is_pending())
    return error::not_ready;
  if (target.component_type != mesh_component_description::type_guid || edit.object ||
      edit.path.size() != 1 || !std::holds_alternative<gneiss::property_id>(edit.path.front()))
    return error::invalid_argument;
  auto captured = document.capture_mesh_binding(locate(document, target));
  if (!captured)
    return captured.error();
  const auto& binding = *captured;
  auto present = document.components(locate(document, target));
  if (!present)
    return present.error();
  if (!present->get<mesh_component_description>())
    return error::not_found;
  mesh_component_description description{std::string(resource_uri(binding.mesh)),
                                         std::string(resource_uri(binding.material))};
  auto root = meta::view(description);
  auto field = meta::resolve_path(root, edit.path);
  auto status = field ? field->assign(edit.data) : field.error();
  if (status.failed())
    return status;
  auto valid = gneiss::serialization::json::encode_bytes(description);
  if (!valid)
    return valid.error().error;
  return request(app, document, history, target, {description.mesh_uri, description.material_uri});
}
gneiss::error editor_asset_edit::request_restore_source(runtime::application& app,
                                                        scene_edit_session& document,
                                                        editor_command_history& history,
                                                        const component_edit_target& target) {
  if (active_ || history.is_pending())
    return gneiss::error::not_ready;
  if (!target.instance_uuid || !document.uri().starts_with("asset://"))
    return gneiss::error::invalid_argument;
  auto status_value = document.serialize();
  if (!status_value)
    return status_value.error();
  const auto& json = *status_value;
  auto changes =
      prepare_restore_prefab_component(json, document.uri().substr(8), target.instance_uuid,
                                       target.source_uuid, target.component_type);
  if (!changes)
    return changes.error();
  restore_ = std::make_shared<scene_document_edit>();
  auto status = restore_->begin(app, document, *changes, "恢复来源组件");
  if (status.failed()) {
    restore_.reset();
    return status;
  }
  active_ = visible_ = true;
  cancelled_ = false;
  operation_ = gneiss::error::not_ready;
  return gneiss::error::success;
}
gneiss::error editor_asset_edit::request_add(runtime::application& application,
                                                     scene_edit_session& session,
                                                     editor_command_history& history,
                                                     std::string_view name,
                                                     mesh_asset_reference assets) {
  return request_impl(application, session, history, {}, name, assets, true);
}
gneiss::error
editor_asset_edit::request_impl(runtime::application& application, scene_edit_session& session,
                                editor_command_history& history,
                                const component_edit_target& target, std::string_view name,
                                mesh_asset_reference assets, bool creating) try {
  if (active_ || history.is_pending()) {
    return gneiss::error::not_ready;
  }
  if (!session.is_open()) {
    return gneiss::error::invalid_state;
  }
  creating_ = creating;
  snapshot_ = {};
  scene_ = session.scene_handle();
  node_ = {};
  target_ = target;
  if (creating_) {
    const auto generated = make_editor_uuid(snapshot_.uuid);
    if (generated.failed()) {
      return generated;
    }
    snapshot_.display_name = name.empty() ? snapshot_.uuid.string() : std::string{name};
    snapshot_.mesh_uri = assets.mesh_uri;
    snapshot_.material_uri = assets.material_uri;
  } else {
    node_ = locate(session, target);
    if (!node_) {
      return gneiss::error::not_found;
    }
  }
  mesh_ = assets.mesh_uri;
  material_ = assets.material_uri;
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  progress_ = {};
  cancelled_ = false;
  visible_ = false;
  operation_ = gneiss::error::success;
  if (target.instance_uuid) {
    // Application 生命周期检查不能由 Scene 线程/句柄检查替代。
    if (!application.is_owner_thread() || !application.accepts_requests())
      return operation_ = gneiss::error::invalid_state;
    auto captured =
        session.capture_component_override(node_, gneiss::mesh_component_description::type_guid);
    if (!captured)
      return operation_ = captured.error();
    prefab_before_ = std::move(*captured);
    previous_ = prefab_before_->resources;
  } else if (!creating_) {
    auto captured = session.capture_mesh_binding(node_);
    if (!captured)
      return operation_ = captured.error();
    previous_ = std::move(*captured);
  }
  const auto runtime_object =
      application.native_world() ? application.native_world()->find(node_) : gneiss::game_object{};
  if (!creating_ && !target.instance_uuid &&
      (!runtime_object.is_valid() || !runtime_object.try_component<gneiss::mesh_component>())) {
    previous_ = {};
    return operation_ = gneiss::error::invalid_state;
  }
  std::vector<gneiss::render_resource_source> sources;
  const auto select = [&](std::string_view uri, gneiss::render_resource_kind type,
                          const auto& previous, auto& next) {
    if (uri.empty())
      next = {};
    else if (gneiss::resource_uri(previous) == uri)
      next = previous;
    else
      sources.push_back({std::string(uri), type});
  };
  select(mesh_, gneiss::render_resource_kind::mesh, previous_.mesh, next_.mesh);
  select(material_, gneiss::render_resource_kind::material, previous_.material, next_.material);
  gneiss::asset_load_completion prepared;
  prepared.state = gneiss::asset_load_state::completed;
  if (!sources.empty()) {
    auto result = application.load_assets(sources);
    if (!result)
      operation_ = result.error();
    else {
      request_ = std::move(*result);
      if (request_.ready()) {
        auto completed = request_.take();
        if (!completed)
          operation_ = completed.error();
        else
          prepared = std::move(*completed);
      } else {
        prepared.state = gneiss::asset_load_state::preparing;
      }
    }
  }
  if (operation_.failed() || prepared.state == gneiss::asset_load_state::completed) {
    if (operation_.ok()) {
      accept_assets(prepared.assets);
      operation_ = commit(application, session, history);
    }
    previous_ = {};
    next_ = {};
    prefab_before_.reset();
    return operation_;
  }
  active_ = true;
  visible_ = true;
  return operation_;
} catch (...) {
  // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  throw;
}

void editor_asset_edit::accept_assets(std::span<const gneiss::render_resource_ref> assets) {
  for (const auto& lease : assets) {
    if (!next_.mesh && gneiss::resource_uri(lease) == mesh_ &&
        gneiss::resource_kind(lease) == gneiss::render_resource_kind::mesh) {
      next_.mesh = gneiss::resource_cast<gneiss::mesh>(lease);
    }
    if (!next_.material && gneiss::resource_uri(lease) == material_ &&
        gneiss::resource_kind(lease) == gneiss::render_resource_kind::material) {
      next_.material = gneiss::resource_cast<gneiss::material>(lease);
    }
  }
}

void editor_asset_edit::advance(runtime::application& application, scene_edit_session& session,
                                editor_command_history& history) {
  if (!active_) {
    return;
  }
  if (restore_) {
    operation_ = restore_->advance(application, session, history);
    if (operation_ == gneiss::error::not_ready)
      return;
    restore_.reset();
    active_ = false;
    visible_ = operation_.failed() && !cancelled_;
    return;
  }
  if (!application.accepts_requests() || !session.is_open() || session.scene_handle() != scene_) {
    operation_ = gneiss::error::invalid_state;
    finish(application);
    return;
  }
  progress_ = request_.progress().value_or(gneiss::asset_load_progress{});
  auto completion = request_.take();
  if (!completion && completion.error() == gneiss::error::not_ready)
    return;
  operation_ = completion ? completion->result : completion.error();
  if (operation_.ok()) {
    accept_assets(completion->assets);
    operation_ = next_.mesh && next_.material ? commit(application, session, history)
                                              : gneiss::error::invalid_state;
  }
  finish(application);
}
void editor_asset_edit::finish(runtime::application&) {
  request_.reset();
  active_ = false;
  visible_ = operation_.failed() && !cancelled_;
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  progress_ = {};
}

void editor_asset_edit::cancel(runtime::application& application) {
  if (!active_) {
    return;
  }
  cancelled_ = true;
  if (restore_) {
    restore_->cancel();
    restore_.reset();
  }
  operation_ = gneiss::error::cancelled;
  finish(application);
}
}
