// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_asset_edit.hpp"
#include "runtime/application.hpp"

namespace gneiss::editor {
result editor_asset_edit::commit_add(runtime::application& application, scene_document& session,
                                     editor_command_history& history) {
  return history.execute({
      .label = "创建 Mesh Renderer 节点",
      .undo =
          [&session, scene = scene_, uuid = snapshot_.uuid] {
            if (session.scene_handle() != scene) {
              return result{result::invalid_state};
            }
            const auto* node = session.find_node(uuid);
            if (node == nullptr) {
              return result{result::not_found};
            }
            scene_node_snapshot discarded;
            return session.destroy_node(node->node, discarded);
          },
      .redo =
          [&application, &session, scene = scene_, snapshot = snapshot_, leases = next_] {
            if (session.scene_handle() != scene) {
              return result{result::invalid_state};
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
                  return from_native(operation);
                },
                output);
          },
      .merge_key = {},
  });
}

result editor_asset_edit::commit(runtime::application& application, scene_document& session,
                                 editor_command_history& history) {
  if (session.scene_handle() != scene_) {
    return result::invalid_state;
  }
  if (creating_) {
    return commit_add(application, session, history);
  }
  const auto apply = [&application, &session, scene = scene_,
                      node = node_](const auto& expected, const auto& replacement) -> result {
    if (session.scene_handle() != scene) {
      return result::invalid_state;
    }
    return session.commit_mesh_renderer(
        node, replacement.mesh.uri(), replacement.material.uri(), [&] {
          return from_native(application.replace_mesh_binding(scene, node, expected, replacement));
        });
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

result editor_asset_edit::request(runtime::application& application, scene_document& session,
                                  editor_command_history& history, std::string_view uuid,
                                  mesh_asset_reference assets) {
  return request_impl(application, session, history, uuid, assets, false);
}
result editor_asset_edit::request_add(runtime::application& application, scene_document& session,
                                      editor_command_history& history, std::string_view name,
                                      mesh_asset_reference assets) {
  return request_impl(application, session, history, name, assets, true);
}
result editor_asset_edit::request_impl(runtime::application& application, scene_document& session,
                                       editor_command_history& history, std::string_view target,
                                       mesh_asset_reference assets, bool creating) try {
  if (active_) {
    return result::not_ready;
  }
  if (!session.is_open()) {
    return result::invalid_state;
  }
  creating_ = creating;
  snapshot_ = {};
  scene_ = session.scene_handle();
  node_ = {};
  if (creating_) {
    const auto generated = make_editor_uuid(snapshot_.uuid);
    if (generated.failed()) {
      return generated;
    }
    snapshot_.display_name = target.empty() ? snapshot_.uuid : std::string{target};
    snapshot_.mesh_uri = assets.mesh_uri;
    snapshot_.material_uri = assets.material_uri;
  } else {
    const auto* node = session.find_node(target);
    if (node == nullptr) {
      return result::not_found;
    }
    node_ = node->node;
  }
  mesh_ = assets.mesh_uri;
  material_ = assets.material_uri;
  previous_ = {};
  next_ = {};
  progress_ = {};
  cancelled_ = false;
  visible_ = false;
  operation_ = creating_ ? result::success
                         : from_native(application.capture_mesh_binding(scene_, node_, previous_));
  if (operation_.failed()) {
    return operation_;
  }
  if (!creating_ && (!previous_.mesh || !previous_.material)) {
    previous_ = {};
    return operation_ = result::invalid_state;
  }
  std::vector<render_internal::render_asset_reload> missing;
  const auto resolve = [&](std::string_view uri, render_internal::render_asset_type type,
                           const auto& previous, auto& next) {
    if (previous.uri() == uri) {
      next = previous;
      return;
    }
    render_internal::render_asset_reload source{.uri = std::string(uri), .type = type};
    const auto ready = application.query_ready_asset(source, next);
    if (ready == GNEISS_ERROR_NOT_READY) {
      missing.push_back(std::move(source));
    } else if (ready != GNEISS_SUCCESS) {
      operation_ = from_native(ready);
    }
  };
  resolve(mesh_, render_internal::render_asset_type::mesh, previous_.mesh, next_.mesh);
  resolve(material_, render_internal::render_asset_type::material, previous_.material,
          next_.material);
  if (operation_.failed()) {
    previous_ = {};
    next_ = {};
    return operation_;
  }
  if (missing.empty()) {
    operation_ = commit(application, session, history);
    previous_ = {};
    next_ = {};
    return operation_;
  }
  operation_ = from_native(application.prepare_edit_assets(missing, ++revision_, request_));
  active_ = operation_.ok();
  visible_ = active_;
  if (!active_) {
    previous_ = {};
    next_ = {};
  }
  return operation_;
} catch (const std::bad_alloc&) {
  previous_ = {};
  next_ = {};
  return operation_ = result::out_of_memory;
} catch (...) {
  previous_ = {};
  next_ = {};
  return operation_ = result::internal;
}

void editor_asset_edit::advance(runtime::application& application, scene_document& session,
                                editor_command_history& history) {
  if (!active_) {
    return;
  }
  (void)application.asset_progress(progress_);
  render_internal::texture_load_completion completion;
  bool ready{};
  const auto polled = application.poll_assets(completion, ready);
  if (polled == GNEISS_SUCCESS && !ready) {
    return;
  }
  operation_ = from_native(polled == GNEISS_SUCCESS ? completion.result : polled);
  if (cancelled_) {
    operation_ = result::invalid_state;
  }
  if (operation_.ok() && completion.request != request_) {
    operation_ = result::invalid_state;
  }
  if (operation_.ok()) {
    for (const auto& lease : completion.assets) {
      if (!next_.mesh && lease.uri() == mesh_ &&
          lease.type() == render_internal::render_asset_type::mesh) {
        next_.mesh = lease;
      }
      if (!next_.material && lease.uri() == material_ &&
          lease.type() == render_internal::render_asset_type::material) {
        next_.material = lease;
      }
    }
    operation_ =
        next_.mesh && next_.material
            ? from_native(application.accept_edit_assets(
                  completion, [&] { return to_native(commit(application, session, history)); }))
            : result::invalid_state;
  }
  active_ = false;
  visible_ = operation_.failed() && !cancelled_;
  previous_ = {};
  next_ = {};
  completion = {};
  (void)application.collect_unused_edit_assets();
}
void editor_asset_edit::cancel(runtime::application& application) {
  if (!active_) {
    return;
  }
  cancelled_ = true;
  // 上传已开始时继续推进至安全终态，回执到达后丢弃候选，不提交编辑。
  (void)application.cancel_assets();
}
}
