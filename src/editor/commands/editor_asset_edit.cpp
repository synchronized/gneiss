// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_asset_edit.hpp"
#include "commands/prefab_authoring.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/instance.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "runtime/application.hpp"
#include <algorithm>

namespace gneiss::editor {
namespace {
scene_node_id locate(scene_document& document, const component_edit_target& target) {
  if (target.instance_uuid) {
    const auto* node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    return node ? node->node : scene_node_id{};
  }
  const auto* node = document.find_node(target.object_uuid);
  return node ? node->node : scene_node_id{};
}
}

gneiss::engine::error editor_asset_edit::commit_add(runtime::application& application,
                                                    scene_document& session,
                                                    editor_command_history& history) {
  return history.execute({
      .label = "创建 Mesh Renderer 节点",
      .undo =
          [&session, scene = scene_, uuid = snapshot_.uuid] {
            if (session.scene_handle() != scene) {
              return gneiss::engine::error{gneiss::engine::error::invalid_state};
            }
            const auto* node = session.find_node(uuid);
            if (node == nullptr) {
              return gneiss::engine::error{gneiss::engine::error::not_found};
            }
            return session.destroy_node(node->node);
          },
      .redo =
          [&application, &session, scene = scene_, snapshot = snapshot_, leases = next_] {
            if (session.scene_handle() != scene) {
              return gneiss::engine::error{gneiss::engine::error::invalid_state};
            }
            scene_node_id output;
            return session.commit_mesh_node(
                snapshot,
                [&](scene_node_id parent, scene_node_id& node) {
                  scene_node_id created{};
                  const auto operation =
                      application.create_mesh_node_ready(scene,
                                                         {
                                                             .uuid = snapshot.uuid,
                                                             .name = snapshot.display_name,
                                                             .parent = parent,
                                                             .mesh_uri = snapshot.mesh_uri,
                                                             .material_uri = snapshot.material_uri,
                                                         },
                                                         leases, created);
                  node = scene_node_id{created};
                  return operation;
                },
                output);
          },
      .merge_key = {},
  });
}

gneiss::engine::error editor_asset_edit::commit(runtime::application& application,
                                                scene_document& session,
                                                editor_command_history& history) {
  if (session.scene_handle() != scene_) {
    return gneiss::engine::error::invalid_state;
  }
  if (creating_) {
    const auto was_dirty = session.is_dirty();
    const auto operation = commit_add(application, session, history);
    if (operation.failed() && !was_dirty)
      session.clear_dirty();
    return operation;
  }
  if (locate(session, target_) != node_)
    return engine::error::invalid_state;
  // 重复设置相同资源不创建历史；来源恢复走文档操作入口。
  if (previous_.mesh == next_.mesh && previous_.material == next_.material)
    return engine::error::success;
  if (prefab_before_) {
    engine::scene_mesh_binding current;
    auto captured = application.capture_mesh_binding(scene_, node_, current);
    if (captured.failed())
      return captured;
    if (current.mesh != previous_.mesh || current.material != previous_.material)
      return engine::error::invalid_state;
    engine::prefab_component_state after;
    after.resources = next_;
    {
      engine::object_description author;
      engine::prefab_asset_ref source;
      auto status = application.scenes()->capture_prefab_header(scene_, target_.instance_uuid,
                                                                author, source);
      if (status.failed())
        return status;
      auto object = std::ranges::find(source->objects, target_.source_uuid,
                                      &engine::object_description::uuid);
      if (object == source->objects.end())
        return engine::error::not_found;
      const auto* original = object->components.get<engine::mesh_renderer_description>();
      engine::mesh_renderer_description description{mesh_, material_};
      if (!original)
        after.added = engine::component_description{description};
      else {
        const std::array values{description.mesh_uri, description.material_uri};
        const std::array originals{original->mesh_uri, original->material_uri};
        for (std::size_t i = 0; i < values.size(); ++i) {
          status = engine::set_prefab_property_override(
              after.properties,
              {.key = {.source_node_uuid = target_.source_uuid,
                       .type_guid = engine::mesh_renderer_description::type_guid,
                       .property_id = engine::meta::property_id{static_cast<std::uint32_t>(i + 1)}},
               .value = engine::value{values[i]}},
              engine::value{originals[i]});
          if (status.failed())
            return status;
        }
      }
    }
    auto apply = [&session, scene = scene_,
                  target = target_](const engine::prefab_component_state& value) {
      if (session.scene_handle() != scene)
        return engine::error{engine::error::invalid_state};
      auto node = locate(session, target);
      return node ? session.set_component_override(
                        node, engine::mesh_renderer_description::type_guid, value)
                  : engine::error{engine::error::not_found};
    };
    return history.execute({.label = "修改实例网格组件",
                            .undo = [apply, before = *prefab_before_] { return apply(before); },
                            .redo = [apply, after] { return apply(after); },
                            .merge_key = {}});
  }
  if (previous_.mesh == next_.mesh && previous_.material == next_.material)
    return engine::error::success;
  const auto apply = [&application, &session, scene = scene_, target = target_](
                         const auto& expected, const auto& replacement) -> gneiss::engine::error {
    if (session.scene_handle() != scene) {
      return gneiss::engine::error::invalid_state;
    }
    const auto node = locate(session, target);
    if (!node)
      return engine::error::not_found;
    return session.commit_mesh_renderer(
        node, gneiss::engine::asset_uri(replacement.mesh),
        gneiss::engine::asset_uri(replacement.material),
        [&] { return application.replace_mesh_binding(scene, node, expected, replacement); });
  };
  const auto was_dirty = session.is_dirty();
  const auto operation = history.execute({
      .label = "替换 Mesh Renderer 资源",
      .undo = [apply, before = previous_, after = next_] { return apply(after, before); },
      .redo = [apply, before = previous_, after = next_] { return apply(before, after); },
      .merge_key = {},
  });
  if (operation.failed() && !was_dirty) {
    session.clear_dirty();
  }
  return operation;
}

engine::error editor_asset_edit::request_remove(runtime::application& application,
                                                scene_document& session,
                                                editor_command_history& history,
                                                gneiss::engine::guid uuid) try {
  if (active_ || history.is_pending())
    return engine::error::not_ready;
  const auto* node = session.find_node(uuid);
  if (!node)
    return engine::error::not_found;
  const auto scene = session.scene_handle();
  engine::scene_mesh_binding binding;
  auto status = application.capture_mesh_binding(scene, node->node, binding);
  if (status.failed())
    return status;
  if (!binding.mesh || !binding.material)
    return engine::error::not_found;
  const auto was_dirty = session.is_dirty();
  status = history.execute({
      .label = "移除 Mesh Renderer",
      .undo =
          [&application, &session, scene, uuid = uuid, binding] {
            if (session.scene_handle() != scene)
              return engine::error::invalid_state;
            const auto* current = session.find_node(uuid);
            if (!current)
              return engine::error::not_found;
            return session.commit_mesh_renderer(current->node, engine::asset_uri(binding.mesh),
                                                engine::asset_uri(binding.material), [&] {
                                                  return application.replace_mesh_binding(
                                                      scene, current->node, {}, binding);
                                                });
          },
      .redo =
          [&session, scene, uuid = uuid] {
            if (session.scene_handle() != scene)
              return engine::error::invalid_state;
            const auto* current = session.find_node(uuid);
            return current ? session.remove_mesh_renderer(current->node) : engine::error::not_found;
          },
      .merge_key = {},
  });
  if (status.failed() && !was_dirty)
    session.clear_dirty();
  return status;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

gneiss::engine::error editor_asset_edit::request(runtime::application& application,
                                                 scene_document& session,
                                                 editor_command_history& history,
                                                 gneiss::engine::guid uuid,
                                                 mesh_asset_reference assets) {
  return request_impl(application, session, history, {.object_uuid = uuid}, {}, assets, false);
}
engine::error editor_asset_edit::request(runtime::application& app, scene_document& document,
                                         editor_command_history& history,
                                         const component_edit_target& target,
                                         mesh_asset_reference assets) {
  return request_impl(app, document, history, target, {}, assets, false);
}
engine::error editor_asset_edit::request_property(runtime::application& app,
                                                  scene_document& document,
                                                  editor_command_history& history,
                                                  const component_edit_target& target,
                                                  const property_edit& edit) try {
  using namespace engine;
  if (active_ || history.is_pending())
    return error::not_ready;
  if (target.component_type != mesh_renderer_description::type_guid || edit.object ||
      edit.path.size() != 1 || !std::holds_alternative<meta::property_id>(edit.path.front()))
    return error::invalid_argument;
  scene_mesh_binding binding;
  auto status =
      app.capture_mesh_binding(document.scene_handle(), locate(document, target), binding);
  if (status.failed())
    return status;
  if (!binding.mesh || !binding.material)
    return error::not_found;
  mesh_renderer_description description{std::string(asset_uri(binding.mesh)),
                                        std::string(asset_uri(binding.material))};
  status =
      meta::write_property(mesh_renderer_description::type_guid,
                           std::get<meta::property_id>(edit.path.front()), &description, edit.data);
  if (status.failed())
    return status;
  auto valid = meta::serialize(description);
  if (!valid)
    return valid.error().error;
  return request(app, document, history, target, {description.mesh_uri, description.material_uri});
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error editor_asset_edit::request_restore_source(runtime::application& app,
                                                        scene_document& document,
                                                        editor_command_history& history,
                                                        const component_edit_target& target) try {
  if (active_ || history.is_pending())
    return engine::error::not_ready;
  if (!target.instance_uuid || !document.uri().starts_with("asset://"))
    return engine::error::invalid_argument;
  std::string json;
  auto status = app.scenes()->serialize(document.scene_handle(), json);
  if (status.failed())
    return status;
  auto changes =
      prepare_restore_prefab_component(json, document.uri().substr(8), target.instance_uuid,
                                       target.source_uuid, target.component_type);
  if (!changes)
    return changes.error();
  restore_ = std::make_shared<scene_document_edit>();
  status = restore_->begin(app, document, *changes, "恢复来源组件");
  if (status.failed()) {
    restore_.reset();
    return status;
  }
  active_ = visible_ = true;
  cancelled_ = false;
  operation_ = engine::error::not_ready;
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
gneiss::engine::error editor_asset_edit::request_add(runtime::application& application,
                                                     scene_document& session,
                                                     editor_command_history& history,
                                                     std::string_view name,
                                                     mesh_asset_reference assets) {
  return request_impl(application, session, history, {}, name, assets, true);
}
gneiss::engine::error
editor_asset_edit::request_impl(runtime::application& application, scene_document& session,
                                editor_command_history& history,
                                const component_edit_target& target, std::string_view name,
                                mesh_asset_reference assets, bool creating) try {
  if (active_ || history.is_pending()) {
    return gneiss::engine::error::not_ready;
  }
  if (!session.is_open()) {
    return gneiss::engine::error::invalid_state;
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
      return gneiss::engine::error::not_found;
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
  operation_ = creating_ ? gneiss::engine::error::success
                         : application.capture_mesh_binding(scene_, node_, previous_);
  if (operation_.failed()) {
    return operation_;
  }
  if (target.instance_uuid) {
    auto captured =
        session.capture_component_override(node_, engine::mesh_renderer_description::type_guid);
    if (!captured)
      return operation_ = captured.error();
    prefab_before_ = std::move(*captured);
  }
  if (!creating_ && !target.instance_uuid && (!previous_.mesh || !previous_.material)) {
    previous_ = {};
    return operation_ = gneiss::engine::error::invalid_state;
  }
  std::vector<gneiss::engine::asset_source> missing;
  const auto resolve = [&](std::string_view uri, gneiss::engine::asset_type type,
                           const auto& previous, auto& next) {
    if (gneiss::engine::asset_uri(previous) == uri) {
      next = previous;
      return;
    }
    gneiss::engine::asset_source source{.uri = std::string(uri), .type = type};
    gneiss::engine::render_asset_ref entry;
    const auto ready = application.query_ready_asset(source, entry);
    using resource_type = std::remove_reference_t<decltype(*next)>;
    next = gneiss::engine::asset_cast<std::remove_const_t<resource_type>>(entry);
    if (ready == gneiss::engine::error::not_ready) {
      missing.push_back(std::move(source));
    } else if (ready != gneiss::engine::error::success) {
      operation_ = ready;
    }
  };
  resolve(mesh_, gneiss::engine::asset_type::mesh, previous_.mesh, next_.mesh);
  resolve(material_, gneiss::engine::asset_type::material, previous_.material, next_.material);
  if (operation_.failed()) {
    previous_ = {};
    next_ = {};
    prefab_before_.reset();
    return operation_;
  }
  if (missing.empty()) {
    operation_ = commit(application, session, history);
    previous_ = {};
    next_ = {};
    prefab_before_.reset();
    return operation_;
  }
  operation_ = application.prepare_edit_assets(missing, ++revision_, request_);
  active_ = operation_.ok();
  visible_ = active_;
  if (!active_) {
    previous_ = {};
    next_ = {};
    prefab_before_.reset();
  }
  return operation_;
} catch (const std::bad_alloc&) {
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  return operation_ = gneiss::engine::error::out_of_memory;
} catch (...) {
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  return operation_ = gneiss::engine::error::internal;
}

void editor_asset_edit::advance(runtime::application& application, scene_document& session,
                                editor_command_history& history) {
  if (!active_) {
    return;
  }
  if (restore_) {
    application.advance_scene_load();
    operation_ = restore_->advance(application, session, history);
    if (operation_ == engine::error::not_ready)
      return;
    restore_.reset();
    active_ = false;
    visible_ = operation_.failed() && !cancelled_;
    return;
  }
  (void)application.asset_progress(progress_);
  gneiss::runtime::asset_load_completion completion;
  bool ready{};
  const auto polled = application.poll_assets(completion, ready);
  if (polled == gneiss::engine::error::success && !ready) {
    return;
  }
  operation_ = polled == gneiss::engine::error::success ? completion.result : polled;
  if (cancelled_) {
    operation_ = gneiss::engine::error::invalid_state;
  }
  if (operation_.ok() && completion.request != request_) {
    operation_ = gneiss::engine::error::invalid_state;
  }
  if (operation_.ok()) {
    for (const auto& lease : completion.assets) {
      if (!next_.mesh && gneiss::engine::asset_uri(lease) == mesh_ &&
          gneiss::engine::asset_kind(lease) == gneiss::engine::asset_type::mesh) {
        next_.mesh = gneiss::engine::asset_cast<gneiss::engine::mesh_resource>(lease);
      }
      if (!next_.material && gneiss::engine::asset_uri(lease) == material_ &&
          gneiss::engine::asset_kind(lease) == gneiss::engine::asset_type::material) {
        next_.material = gneiss::engine::asset_cast<gneiss::engine::material_resource>(lease);
      }
    }
    operation_ = next_.mesh && next_.material ? commit(application, session, history)
                                              : gneiss::engine::error::invalid_state;
  }
  active_ = false;
  visible_ = operation_.failed() && !cancelled_;
  previous_ = {};
  next_ = {};
  prefab_before_.reset();
  completion = {};
  (void)application.collect_unused_edit_assets();
}
void editor_asset_edit::cancel(runtime::application& application) {
  if (!active_) {
    return;
  }
  cancelled_ = true;
  if (restore_) {
    restore_->cancel();
    return;
  }
  // 上传已开始时继续推进至安全终态，回执到达后丢弃候选，不提交编辑。
  (void)application.cancel_assets();
}
}
