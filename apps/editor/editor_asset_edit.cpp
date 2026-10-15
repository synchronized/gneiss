// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "editor_asset_edit.hpp"

namespace gneiss::editor {
result editor_asset_edit::commit(gneiss_application application, editor_session& session,
                                 editor_command_history& history) {
  if (session.scene_handle() != scene_) {
    return result::invalid_state;
  }
  const auto apply = [application, &session, scene = scene_,
                      node = node_](const auto& expected, const auto& replacement) -> result {
    if (session.scene_handle() != scene) {
      return result::invalid_state;
    }
    return session.commit_mesh_renderer(
        node, replacement.mesh.uri(), replacement.material.uri(), [&] {
          return from_native(application_internal::replace_mesh_binding(
              application, scene, node.get(), expected, replacement));
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

result editor_asset_edit::request(gneiss_application application, editor_session& session,
                                  editor_command_history& history, std::string_view uuid,
                                  mesh_asset_reference assets) try {
  if (active_) {
    return result::not_ready;
  }
  const auto* node = session.find_node(uuid);
  if (node == nullptr) {
    return result::not_found;
  }
  scene_ = session.scene_handle();
  node_ = node->node;
  mesh_ = assets.mesh_uri;
  material_ = assets.material_uri;
  previous_ = {};
  next_ = {};
  progress_ = {};
  cancelled_ = false;
  visible_ = false;
  operation_ = from_native(
      application_internal::capture_mesh_binding(application, scene_, node_.get(), previous_));
  if (operation_.failed()) {
    return operation_;
  }
  if (!previous_.mesh || !previous_.material) {
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
    const auto ready = application_internal::query_ready_asset(application, source, next);
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
  operation_ = from_native(
      application_internal::prepare_edit_assets(application, missing, ++revision_, request_));
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

void editor_asset_edit::advance(gneiss_application application, editor_session& session,
                                editor_command_history& history) {
  if (!active_) {
    return;
  }
  bool available{};
  (void)application_internal::query_asset_load_progress(application, progress_, available);
  render_internal::texture_load_completion completion;
  bool ready{};
  const auto polled = application_internal::poll_textures(application, completion, ready);
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
    operation_ = next_.mesh && next_.material
                     ? from_native(application_internal::accept_edit_assets(
                           application, completion,
                           [&] { return to_native(commit(application, session, history)); }))
                     : result::invalid_state;
  }
  active_ = false;
  visible_ = operation_.failed() && !cancelled_;
  previous_ = {};
  next_ = {};
  completion = {};
  (void)application_internal::collect_unused_edit_assets(application);
}
void editor_asset_edit::cancel(gneiss_application application) {
  if (!active_) {
    return;
  }
  cancelled_ = true;
  // 上传已开始时继续推进至安全终态，回执到达后丢弃候选，不提交编辑。
  (void)application_internal::cancel_render_assets(application);
}
}
