// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_prefab_edit.hpp"
#include "application/application.hpp"
#include "documents/scene_document_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <algorithm>

namespace gneiss::editor {
namespace {
using prefab_snapshots = std::vector<std::shared_ptr<const scene_subtree_snapshot>>;
engine::error capture_document_prefab(scene_edit_session& document, engine::guid uuid,
                                      std::shared_ptr<scene_subtree_snapshot>& output) {
  auto node = document.find_prefab_root(uuid);
  if (!node)
    return node.error();
  auto captured = document.capture_subtree(node->node);
  if (!captured)
    return captured.error();
  output = std::make_shared<scene_subtree_snapshot>(std::move(*captured));
  return engine::error::success;
}

}
struct editor_prefab_edit::state {
  explicit state(runtime::application& host) : application(host) {}
  runtime::application& application;
  gneiss::scene_instance_id scene{};
  scene_edit_session* session{};
  editor_command_history* history{};
  engine::asset_request<engine::prefab> preparation;
  prefab_header_snapshot source;
  std::vector<gneiss::engine::render_resource_ref> assets;
  scene_edit_session::version base;
  std::shared_ptr<scene_subtree_snapshot> captured;
  bool removing{}, refreshing{};
  std::vector<engine::guid> refresh_targets;
  prefab_snapshots before, after;
  gneiss::engine::guid target_uuid;
  engine::asset_batch_request request;
  bool asset_pending{}, cancelled{}, active{}, visible{};
  gneiss::engine::error status{gneiss::engine::error::success};
  gneiss::engine::error failure{gneiss::engine::error::success};
  void finish() {
    request.reset();
    asset_pending = false;
    active = false;
    if (failure.failed()) {
      status = failure;
    }
    visible = status.failed() && (!cancelled || failure.failed());
    base = {};
    captured.reset();
    before.clear();
    after.clear();
    source = {};
    assets.clear();
  }

  engine::result<scene_subtree_snapshot> freeze(prefab_header_snapshot source) const {
    if (!base)
      return engine::error::invalid_state;
    auto retained = base.content().assets;
    for (const auto& asset : assets)
      retained.insert_or_assign(std::string(engine::resource_uri(asset)), asset);
    return freeze_prefab_snapshot(std::move(source), retained);
  }

  void prepare_refresh() {

    if (before.size() < refresh_targets.size()) {
      status = capture_document_prefab(*session, refresh_targets[before.size()], captured);
      if (status.failed()) {
        finish();
        return;
      }
      before.push_back(std::move(captured));
      return;
    }
    if (after.size() < before.size()) {
      auto refreshed = source;
      auto description = before[after.size()]->description();
      if (!description) {
        status = description.error();
        finish();
        return;
      }
      auto author = std::ranges::find(description->objects, before[after.size()]->root_uuid,
                                      &engine::game_object_description::uuid);
      if (author == description->objects.end()) {
        status = engine::error::invalid_argument;
        finish();
        return;
      }
      refreshed.author = *author;
      auto snapshot = freeze(std::move(refreshed));
      if (!snapshot) {
        status = snapshot.error();
        finish();
        return;
      }
      // 刷新只更换模板来源，自有子对象由现有实例化入口保留。
      auto adjusted = std::make_shared<scene_subtree_snapshot>(std::move(*snapshot));
      adjusted->parent = before[after.size()]->parent;
      after.push_back(std::move(adjusted));
      return;
    }
    const auto selected = session->selected_prefab_node();
    const auto ordinary = session->selected_node();
    status = execute_scene_edit(*history, *session, "刷新同源 Prefab 实例", [&] {
      if (session->current_version() != base)
        return engine::error{engine::error::invalid_state};
      for (const auto& snapshot : after) {
        auto applied = session->apply_prefab(*snapshot, false, true);
        if (applied.failed())
          return applied;
      }
      if (selected) {
        auto node =
            session->find_prefab_source(selected->instance_uuid, selected->source_node_uuid);
        if (node)
          (void)session->select(node->node);
      } else if (ordinary) {
        auto node = session->find_node(ordinary->uuid);
        if (node)
          (void)session->select(node->node);
      }
      return engine::error{engine::error::success};
    });
    finish();
  }

  bool poll_preparation() {

    auto loaded = preparation.take();
    if (!loaded && loaded.error().error == engine::error::not_ready)
      return false;
    status = loaded ? engine::error::success : loaded.error().error;
    preparation.cancel();
    std::vector<engine::render_resource_source> requested;
    if (loaded) {
      source.source = (*loaded)->snapshot();
      source.source_asset = *loaded;
      engine::scene_description input;
      input.objects = source.source->objects;
      auto composed = engine::compose_scene(input, {});
      if (!composed)
        status = composed.error();
      else {
        auto dependencies = engine::scene_dependencies(*composed);
        if (!dependencies)
          status = dependencies.error();
        else
          requested = std::move(*dependencies);
      }
    }
    if (status.ok() && !cancelled) {
      auto submitted = application.load_assets(requested, refreshing);
      if (!submitted)
        status = submitted.error();
      else {
        request = std::move(*submitted);
        asset_pending = !request.ready();
        if (asset_pending)
          return false;
        auto completed = request.take();
        if (!completed)
          status = completed.error();
        else {
          status = completed->result;
          assets = std::move(completed->assets);
        }
      }
    }

    return true;
  }
  bool receive_assets() {
    auto polled = request.take();
    if (!polled) {
      status = polled.error();
      return status != engine::error::not_ready;
    }
    asset_pending = false;
    status = polled->result;
    if (status.ok())
      assets = std::move(polled->assets);
    return true;
  }
};
editor_prefab_edit::editor_prefab_edit() = default;
editor_prefab_edit::~editor_prefab_edit() = default;
bool editor_prefab_edit::active() const noexcept { return state_ && state_->active; }
bool editor_prefab_edit::visible() const noexcept { return state_ && state_->visible; }
bool editor_prefab_edit::can_cancel() const noexcept { return active(); }
prefab_edit_phase editor_prefab_edit::phase() const noexcept {
  if (!active()) {
    return prefab_edit_phase::finished;
  }
  if (state_->cancelled) {
    return prefab_edit_phase::cleanup;
  }
  if (state_->preparation) {
    return prefab_edit_phase::preparing;
  }
  if (state_->asset_pending) {
    return prefab_edit_phase::assets;
  }
  return prefab_edit_phase::constructing;
}
gneiss::engine::error editor_prefab_edit::operation() const noexcept {
  return state_ ? state_->status : gneiss::engine::error::success;
}
void editor_prefab_edit::dismiss() noexcept {
  if (!active()) {
    state_.reset();
  }
}
gneiss::engine::error editor_prefab_edit::request(runtime::application& application,
                                                  gneiss::engine::task_executor& executor,
                                                  scene_edit_session& session,
                                                  editor_command_history& history,
                                                  std::string_view name, std::string_view uri,
                                                  entity_id parent) {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.is_owner_thread() || !application.accepts_requests())
    return engine::error::invalid_state;
  auto next = std::make_unique<state>(application);
  next->scene = session.scene_handle();
  next->session = &session;
  next->base = session.current_version();
  next->history = &history;
  next->source.author.prefab.emplace();
  auto status = make_editor_uuid(next->source.author.uuid);
  if (status.failed()) {
    return status;
  }
  next->source.author.name = name;
  next->source.author.prefab->uri = uri;
  if (parent.is_valid()) {
    auto snapshot = session.capture_nodes();
    if (!snapshot)
      return snapshot.error();
    const auto found = std::ranges::find(snapshot->nodes, parent, &scene_node_record::node);
    if (found != snapshot->nodes.end()) {
      next->source.author.parent = {found->uuid, {}};
    } else {
      const auto source =
          std::ranges::find(snapshot->prefab_nodes, parent, &prefab_node_record::node);
      if (source == snapshot->prefab_nodes.end())
        return engine::error::invalid_handle;
      next->source.author.parent = {source->instance_uuid, source->source_node_uuid};
    }
  }
  status = application.assets().configure(executor);
  if (status.failed())
    return status;
  auto loaded = application.assets().reload<engine::prefab>(uri);
  if (!loaded)
    return loaded.error();
  next->preparation = std::move(*loaded);
  next->active = next->visible = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
}
gneiss::engine::error editor_prefab_edit::request_remove(runtime::application& application,
                                                         scene_edit_session& session,
                                                         editor_command_history& history,
                                                         entity_id root) {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.is_owner_thread() || !application.accepts_requests())
    return engine::error::invalid_state;
  auto snapshot = session.capture_nodes();
  if (!snapshot)
    return snapshot.error();
  const auto found = std::ranges::find(snapshot->prefab_nodes, root, &prefab_node_record::node);
  if (found == snapshot->prefab_nodes.end() || !found->is_instance_root) {
    return gneiss::engine::error::invalid_handle;
  }
  auto next = std::make_unique<state>(application);
  next->session = &session;
  next->base = session.current_version();
  next->history = &history;
  next->scene = session.scene_handle();
  next->target_uuid = found->instance_uuid;
  next->active = next->visible = next->removing = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
}

gneiss::engine::error editor_prefab_edit::request_refresh(runtime::application& application,
                                                          gneiss::engine::task_executor& executor,
                                                          scene_edit_session& session,
                                                          editor_command_history& history,
                                                          entity_id root) {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.is_owner_thread() || !application.accepts_requests())
    return engine::error::invalid_state;
  auto snapshot = session.capture_nodes();
  if (!snapshot)
    return snapshot.error();
  const auto found = std::ranges::find(snapshot->prefab_nodes, root, &prefab_node_record::node);
  if (found == snapshot->prefab_nodes.end() || !found->is_instance_root) {
    return gneiss::engine::error::invalid_handle;
  }
  auto next = std::make_unique<state>(application);
  next->scene = session.scene_handle();
  next->session = &session;
  next->base = session.current_version();
  next->history = &history;
  next->source.author.prefab.emplace();
  next->source.author.prefab->uri = found->prefab_uri;
  for (const auto& record : snapshot->prefab_nodes) {
    if (record.is_instance_root && record.prefab_uri == found->prefab_uri) {
      next->refresh_targets.push_back(record.instance_uuid);
    }
  }
  const auto status = application.assets().configure(executor);
  if (status.failed())
    return status;
  auto loaded = application.assets().reload<engine::prefab>(found->prefab_uri);
  if (!loaded)
    return loaded.error();
  next->preparation = std::move(*loaded);
  next->active = next->visible = next->refreshing = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
}

void editor_prefab_edit::cancel() {
  if (!can_cancel()) {
    return;
  }
  auto& value = *state_;
  value.cancelled = true;
  if (value.preparation) {
    value.preparation.cancel();
  }
  if (value.asset_pending) {
    value.status = engine::error::cancelled;
    value.finish();
    return;
  }
}
void editor_prefab_edit::advance() {
  if (!active()) {
    return;
  }
  auto& value = *state_;
  try {
    if (value.preparation && !value.poll_preparation()) {
      return;
    }
    if (value.asset_pending &&
        (!value.application.accepts_requests() || value.session->scene_handle() != value.scene)) {
      value.status = engine::error::invalid_state;
      value.finish();
      return;
    }
    if (value.asset_pending && !value.receive_assets()) {
      return;
    }
    if (value.cancelled || value.session->scene_handle() != value.scene) {
      value.status = gneiss::engine::error::invalid_state;
    }
    if (value.status.failed()) {
      value.finish();
      return;
    }
    if (value.refreshing) {
      value.prepare_refresh();
      return;
    }
    std::shared_ptr<const scene_subtree_snapshot> snapshot;
    if (value.removing) {
      value.status = capture_document_prefab(*value.session, value.target_uuid, value.captured);
      snapshot = value.captured;
    } else {
      auto frozen = value.freeze(std::move(value.source));
      if (!frozen)
        value.status = frozen.error();
      else
        snapshot = std::make_shared<scene_subtree_snapshot>(std::move(*frozen));
    }
    if (value.status.ok()) {
      value.status = value.history->prepare_record();
    }
    if (value.status.failed()) {
      value.finish();
      return;
    }
    value.status =
        execute_scene_edit(*value.history, *value.session,
                           value.removing ? "删除 Prefab 实例" : "放置 Prefab 实例", [&] {
                             if (value.session->current_version() != value.base)
                               return engine::error{engine::error::invalid_state};
                             return value.session->apply_prefab(*snapshot, value.removing, false);
                           });
    value.finish();
  } catch (...) {
    // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
    value.failure = engine::error::internal;
    value.status = value.failure;
    cancel();
    throw;
  }
}
}
