// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_prefab_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/prefab_placement.hpp"
#include "runtime/loading/prefab_preparation.hpp"
#include <algorithm>

namespace gneiss::editor {
namespace {
using runtime::prefab_placement;
using runtime::prefab_placement_snapshot;
using prefab_snapshots = std::vector<std::shared_ptr<const prefab_placement_snapshot>>;
class prefab_history_operation : public editor_history_operation {
public:
  prefab_history_operation(runtime::application& application, scene_document& session,
                           scene_instance_id scene,
                           std::shared_ptr<const prefab_placement_snapshot> snapshot, bool removing)
      : prefab_history_operation(application, session, scene, prefab_snapshots{std::move(snapshot)},
                                 removing, false) {}
  prefab_history_operation(runtime::application& application, scene_document& session,
                           scene_instance_id scene, prefab_snapshots snapshots, bool removing,
                           bool replacing)
      : application_(application), session_(session), scene_(scene),
        snapshots_(std::move(snapshots)), removing_(removing), replacing_(replacing) {}
  gneiss::engine::error advance(bool& complete) noexcept override try {
    using gneiss::engine::error;
    complete = true;
    if (finished_)
      return error::invalid_state;
    finished_ = true;
    if (cancelled_ || session_.scene_handle() != scene_)
      return error::invalid_state;
    std::string selected_instance, selected_source;
    if (const auto* selected = session_.selected_prefab_node()) {
      selected_instance = selected->instance_uuid;
      selected_source = selected->source_node_uuid;
    }
    scene_node_id root;
    auto status = error{error::success};
    // 同一调用内逐个创建；前面的成功不会因后面的失败回滚。
    for (const auto& snapshot : snapshots_) {
      status =
          prefab_placement::apply(application_, scene_, *snapshot, removing_, replacing_, root);
      if (status.failed())
        break;
    }
    session_.mark_dirty();
    const auto refreshed = session_.synchronize_nodes();
    if (refreshed.failed())
      return refreshed;
    if (replacing_ && !selected_instance.empty()) {
      const auto* selected = session_.find_prefab_source(selected_instance, selected_source);
      if (!selected)
        selected = session_.find_prefab_root(selected_instance);
      if (selected)
        (void)session_.select(selected->node);
    } else if (!removing_ && root) {
      (void)session_.select(root);
    }
    return status;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
  void cancel() noexcept override {
    if (!finished_)
      cancelled_ = true;
  }
  bool can_cancel() const noexcept override { return !finished_; }

private:
  runtime::application& application_;
  scene_document& session_;
  scene_instance_id scene_;
  prefab_snapshots snapshots_;
  bool removing_{}, replacing_{}, finished_{}, cancelled_{};
};
class prefab_refresh_operation final : public prefab_history_operation {
public:
  prefab_refresh_operation(runtime::application& application, scene_document& session,
                           scene_instance_id scene, prefab_snapshots snapshots)
      : prefab_history_operation(application, session, scene, std::move(snapshots), false, true) {}
};
class prefab_refresh_command final : public editor_command {
public:
  prefab_refresh_command(runtime::application& application, scene_document& session,
                         gneiss::scene_instance_id scene, prefab_snapshots before,
                         prefab_snapshots after)
      : application_(application), scene_(scene), session_(session), before_(std::move(before)),
        after_(std::move(after)) {}
  [[nodiscard]] std::string_view label() const noexcept override { return "刷新同源 Prefab 实例"; }
  gneiss::engine::error undo() noexcept override { return gneiss::engine::error::not_ready; }
  gneiss::engine::error redo() noexcept override { return gneiss::engine::error::not_ready; }
  gneiss::engine::error
  begin_history_operation(bool undo,
                          std::unique_ptr<editor_history_operation>& output) noexcept override try {
    if (session_.scene_handle() != scene_) {
      return gneiss::engine::error::invalid_state;
    }
    output = std::make_unique<prefab_refresh_operation>(application_, session_, scene_,
                                                        undo ? before_ : after_);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }

private:
  runtime::application& application_;
  gneiss::scene_instance_id scene_{};
  scene_document& session_;
  prefab_snapshots before_, after_;
};
class prefab_command final : public editor_command {
public:
  prefab_command(runtime::application& application, scene_document& session,
                 gneiss::scene_instance_id scene,
                 std::shared_ptr<const prefab_placement_snapshot> snapshot, bool removal = false)
      : application_(application), scene_(scene), session_(session), snapshot_(std::move(snapshot)),
        removal_(removal) {}
  [[nodiscard]] std::string_view label() const noexcept override {
    return removal_ ? "删除 Prefab 实例" : "放置 Prefab 实例";
  }
  gneiss::engine::error undo() noexcept override { return gneiss::engine::error::not_ready; }
  gneiss::engine::error redo() noexcept override { return gneiss::engine::error::not_ready; }
  gneiss::engine::error
  begin_history_operation(bool undo,
                          std::unique_ptr<editor_history_operation>& output) noexcept override try {
    if (session_.scene_handle() != scene_) {
      return gneiss::engine::error::invalid_state;
    }
    output = std::make_unique<prefab_history_operation>(application_, session_, scene_, snapshot_,
                                                        undo != removal_);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }

private:
  runtime::application& application_;
  gneiss::scene_instance_id scene_{};
  scene_document& session_;
  std::shared_ptr<const prefab_placement_snapshot> snapshot_;
  bool removal_{};
};
}
struct editor_prefab_edit::state {
  explicit state(runtime::application& host) : application(host) {}
  runtime::application& application;
  gneiss::scene_instance_id scene{};
  scene_document* session{};
  editor_command_history* history{};
  std::unique_ptr<runtime::prefab_preparation> preparation;
  gneiss::engine::prefab_instance_description author;
  gneiss::engine::prepared_scene_description prepared;
  std::vector<gneiss::engine::render_asset_ref> assets;
  gneiss::runtime::asset_load_completion completion;
  std::unique_ptr<editor_command> command;
  std::unique_ptr<editor_history_operation> operation;
  std::shared_ptr<prefab_placement_snapshot> captured;
  bool removing{}, refreshing{};
  std::vector<std::string> refresh_targets;
  prefab_snapshots before, after;
  std::string target_uuid;
  std::uint64_t request{};
  bool asset_pending{}, cancelled{}, active{}, visible{};
  gneiss::engine::error status{gneiss::engine::error::success};
  gneiss::engine::error failure{gneiss::engine::error::success};
  void finish() noexcept {
    active = false;
    if (failure.failed()) {
      status = failure;
    }
    visible = status.failed() && (!cancelled || failure.failed());
    operation.reset();
    command.reset();
    captured.reset();
    before.clear();
    after.clear();
    completion = {};
    prepared.prefabs.clear();
    prepared.assets.clear();
    prepared.parent_first.clear();
    assets.clear();
    (void)application.collect_unused_edit_assets();
  }

  void prepare_refresh() {

    if (before.size() < refresh_targets.size()) {
      bool complete{};
      status = prefab_placement::capture_step(application, refresh_targets[before.size()], scene,
                                              captured, complete);
      if (status.failed()) {
        finish();
        return;
      }
      if (complete) {
        before.push_back(std::move(captured));
      }
      return;
    }
    if (after.size() < before.size()) {
      std::shared_ptr<const prefab_placement_snapshot> snapshot;
      status = prefab_placement::freeze(application, prepared.prefabs.at(author.prefab_uri),
                                        before[after.size()]->author, assets, snapshot);
      if (status.failed()) {
        finish();
        return;
      }
      after.push_back(std::move(snapshot));
      return;
    }
    status = history->prepare_record();
    if (status.failed()) {
      finish();
      return;
    }
    command = std::make_unique<prefab_refresh_command>(application, *session, scene, before, after);
    operation = std::make_unique<prefab_refresh_operation>(application, *session, scene, after);
  }

  bool poll_preparation() {

    bool complete{};
    status = preparation->poll(prepared, complete);
    if (!complete) {
      return false;
    }
    preparation.reset();
    if (status.ok() && !cancelled) {
      std::vector<gneiss::engine::asset_source> missing;
      for (const auto& asset : prepared.assets) {
        gneiss::engine::render_asset_ref lease;
        const auto ready = refreshing ? gneiss::engine::error::not_ready
                                      : application.query_ready_asset(asset, lease);
        if (ready == gneiss::engine::error::success) {
          assets.push_back(std::move(lease));
        } else if (ready == gneiss::engine::error::not_ready) {
          missing.push_back(asset);
        } else {
          status = ready;
          break;
        }
      }
      if (status.ok() && !missing.empty()) {
        status = application.prepare_edit_assets(missing, 1U, request);
        asset_pending = status.ok();
        if (asset_pending) {
          return false;
        }
      }
    }

    return true;
  }
  bool poll_assets() {

    bool ready{};
    status = application.poll_assets(completion, ready);
    if (status.ok() && !ready) {
      return false;
    }
    asset_pending = false;
    if (status.ok()) {
      status = completion.result;
    }
    if (status.ok() && completion.request != request) {
      status = gneiss::engine::error::invalid_state;
    }
    if (status.ok()) {
      assets.insert(assets.end(), completion.assets.begin(), completion.assets.end());
    }

    return true;
  }
};
editor_prefab_edit::editor_prefab_edit() = default;
editor_prefab_edit::~editor_prefab_edit() = default;
bool editor_prefab_edit::active() const noexcept { return state_ && state_->active; }
bool editor_prefab_edit::visible() const noexcept { return state_ && state_->visible; }
bool editor_prefab_edit::can_cancel() const noexcept {
  return active() && (!state_->operation || state_->operation->can_cancel());
}
prefab_edit_phase editor_prefab_edit::phase() const noexcept {
  if (!active()) {
    return prefab_edit_phase::finished;
  }
  if (state_->cancelled || (state_->operation && !state_->operation->can_cancel())) {
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
                                                  scene_document& session,
                                                  editor_command_history& history,
                                                  std::string_view name, std::string_view uri,
                                                  scene_node_id parent) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  auto next = std::make_unique<state>(application);
  next->scene = session.scene_handle();
  next->session = &session;
  next->history = &history;
  auto status = make_editor_uuid(next->author.instance_uuid);
  if (status.failed()) {
    return status;
  }
  next->author.name = name;
  next->author.prefab_uri = uri;
  next->author.rotation[3] = 1.0F;
  next->author.scale = {1.0F, 1.0F, 1.0F};
  if (parent.is_valid()) {
    const auto found = std::ranges::find(session.nodes(), parent, &scene_node_record::node);
    if (found == session.nodes().end()) {
      return gneiss::engine::error::invalid_handle;
    }
    next->author.parent_uuid = found->uuid;
  }
  status = runtime::prefab_preparation::create(application, executor, uri, next->preparation);
  if (status.failed()) {
    return status;
  }
  next->active = next->visible = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
gneiss::engine::error editor_prefab_edit::request_remove(runtime::application& application,
                                                         scene_document& session,
                                                         editor_command_history& history,
                                                         scene_node_id root) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  const auto found = std::ranges::find(session.prefab_nodes(), root, &prefab_node_record::node);
  if (found == session.prefab_nodes().end() || !found->is_instance_root) {
    return gneiss::engine::error::invalid_handle;
  }
  auto next = std::make_unique<state>(application);
  next->session = &session;
  next->history = &history;
  next->scene = session.scene_handle();
  next->target_uuid = found->instance_uuid;
  next->active = next->visible = next->removing = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error editor_prefab_edit::request_refresh(runtime::application& application,
                                                          gneiss::engine::task_executor& executor,
                                                          scene_document& session,
                                                          editor_command_history& history,
                                                          scene_node_id root) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return gneiss::engine::error::not_ready;
  }
  const auto found = std::ranges::find(session.prefab_nodes(), root, &prefab_node_record::node);
  if (found == session.prefab_nodes().end() || !found->is_instance_root) {
    return gneiss::engine::error::invalid_handle;
  }
  auto next = std::make_unique<state>(application);
  next->scene = session.scene_handle();
  next->session = &session;
  next->history = &history;
  next->author.prefab_uri = found->prefab_uri;
  for (const auto& record : session.prefab_nodes()) {
    if (record.is_instance_root && record.prefab_uri == found->prefab_uri) {
      next->refresh_targets.push_back(record.instance_uuid);
    }
  }
  const auto status = runtime::prefab_preparation::create(application, executor, found->prefab_uri,
                                                          next->preparation);
  if (status.failed()) {
    return status;
  }
  next->active = next->visible = next->refreshing = true;
  state_ = std::move(next);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

void editor_prefab_edit::cancel() {
  if (!can_cancel()) {
    return;
  }
  auto& value = *state_;
  value.cancelled = true;
  if (value.preparation) {
    value.preparation->cancel();
  }
  if (value.asset_pending) {
    (void)value.application.cancel_assets();
  }
  if (value.operation) {
    value.operation->cancel();
  }
}
void editor_prefab_edit::advance() {
  if (!active()) {
    return;
  }
  auto& value = *state_;
  try {
    if (value.operation) {
      bool complete{};
      value.status = value.operation->advance(complete);
      if (!complete) {
        return;
      }
      if (value.status.ok()) {
        value.status = value.history->record(std::move(value.command));
      }
      value.finish();
      return;
    }
    if (value.preparation && !value.poll_preparation()) {
      return;
    }
    if (value.asset_pending && !value.poll_assets()) {
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
    std::shared_ptr<const prefab_placement_snapshot> snapshot;
    if (value.removing) {
      bool complete{};
      value.status = prefab_placement::capture_step(value.application, value.target_uuid,
                                                    value.scene, value.captured, complete);
      if (value.status.ok() && !complete) {
        return;
      }
      snapshot = value.captured;
    } else {
      value.status = prefab_placement::freeze(
          value.application, std::move(value.prepared.prefabs.at(value.author.prefab_uri)),
          value.author, std::move(value.assets), snapshot);
    }
    if (value.status.ok()) {
      value.status = value.history->prepare_record();
    }
    if (value.status.failed()) {
      value.finish();
      return;
    }
    value.command = std::make_unique<prefab_command>(value.application, *value.session, value.scene,
                                                     snapshot, value.removing);
    value.operation = std::make_unique<prefab_history_operation>(
        value.application, *value.session, value.scene, std::move(snapshot), value.removing);
  } catch (const std::bad_alloc&) {
    value.failure = gneiss::engine::error::out_of_memory;
    value.status = value.failure;
    cancel();
  } catch (...) {
    value.failure = gneiss::engine::error::internal;
    value.status = value.failure;
    cancel();
  }
}
}
