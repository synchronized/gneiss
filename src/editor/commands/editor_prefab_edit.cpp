// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "commands/editor_prefab_edit.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/prefab_placement.hpp"
#include "runtime/loading/prefab_preparation.hpp"
#include <algorithm>

namespace gneiss::editor {
namespace {
using runtime::prefab_placement;
using runtime::prefab_placement_snapshot;
result make_prefab_record(const engine::scene::prefab_node_view& info,
                          const engine::resource::prefab_instance_description& author,
                          const scene_document& session, prefab_node_record& record) {
  record.node = scene_node_id{info.node};
  record.parent = scene_node_id{info.parent};
  record.entity = entity_id{info.entity};
  record.instance_uuid = info.instance_uuid;
  record.source_node_uuid = info.source_node_uuid;
  record.display_name = info.name;
  record.prefab_uri = info.prefab_uri;
  record.local_transform = {info.local_transform.translation, info.local_transform.rotation,
                            info.local_transform.scale};
  record.source_local_transform = {info.source_local_transform.translation,
                                   info.source_local_transform.rotation,
                                   info.source_local_transform.scale};
  record.is_instance_root = has_flags(info.flags, scene_prefab_flags::instance_root);
  record.is_read_only = !record.is_instance_root;
  record.override_flags =
      static_cast<std::uint32_t>(info.flags) &
      (GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN |
       GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN | GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN);
  if (record.is_instance_root && author.parent_uuid) {
    const auto* parent = session.find_node(*author.parent_uuid);
    if (parent == nullptr) {
      return result::invalid_handle;
    }
    record.parent = parent->node;
  }
  return result::success;
}
class prefab_history_operation final : public editor_history_operation {
public:
  prefab_history_operation(
      runtime::application& application, scene_document& session, gneiss_scene_instance scene,
      std::shared_ptr<const prefab_placement_snapshot> snapshot, bool removing,
      std::function<gneiss_result(const std::function<gneiss_result()>&)> accept = {})
      : application_(application), scene_(scene), session_(session), snapshot_(std::move(snapshot)),
        removing_(removing), accept_(std::move(accept)) {}
  result advance(bool& complete) noexcept override try {
    complete = false;
    if (committed_ || cancelled_ || status_.failed()) {
      return cleanup(complete);
    }
    if (session_.scene_handle() != scene_) {
      status_ = result::invalid_state;
      return result::success;
    }
    if (!transaction_) {
      status_ = from_native(
          prefab_placement::begin(application_, scene_, snapshot_, removing_, transaction_));
      if (status_.failed()) {
        return result::success;
      }
      records_.reserve(session_.prefab_nodes().size() +
                       (removing_ ? 0U : transaction_->node_count()));
    }
    bool ready{};
    status_ = from_native(transaction_->advance(ready));
    if (status_.failed() || !ready) {
      return result::success;
    }
    return prepare_mirror();
  } catch (const std::bad_alloc&) {
    status_ = result::out_of_memory;
    return result::success;
  } catch (...) {
    status_ = result::internal;
    return result::success;
  }
  void cancel() noexcept override {
    if (!committed_) {
      cancelled_ = true;
    }
  }
  [[nodiscard]] bool can_cancel() const noexcept override { return !committed_; }

private:
  result cleanup(bool& complete) {
    if (transaction_ && !transaction_->cleanup_step()) {
      return result::success;
    }
    for (unsigned i = 0; i < 64U && !records_.empty(); ++i) {
      records_.pop_back();
    }
    complete = records_.empty();
    if (!complete || committed_) {
      return result::success;
    }
    return cancelled_ ? result::invalid_state : status_;
  }
  result append_record() {
    engine::scene::prefab_node_view info;
    const auto previewed = from_native(transaction_->preview(new_cursor_++, info));
    if (previewed.failed()) {
      return previewed;
    }
    prefab_node_record record;
    const auto converted = make_prefab_record(info, snapshot_->author, session_, record);
    if (converted.failed()) {
      return converted;
    }
    records_.push_back(std::move(record));
    return result::success;
  }
  result prepare_mirror() {
    for (unsigned step = 0; step < 64U; ++step) {
      if (old_cursor_ < session_.prefab_nodes().size()) {
        const auto& record = session_.prefab_nodes()[old_cursor_++];
        if (!removing_ || record.instance_uuid != snapshot_->author.instance_uuid) {
          records_.push_back(record);
        }
      } else if (!removing_ && new_cursor_ < transaction_->node_count()) {
        const auto added = append_record();
        if (added.failed()) {
          status_ = added;
          return result::success;
        }
      } else {
        const auto commit = [&] { return transaction_->commit(); };
        status_ = from_native(accept_ ? accept_(commit) : commit());
        if (status_.failed()) {
          return result::success;
        }
        session_.publish_prefab_records(records_, removing_ ? scene_node_id{}
                                                            : scene_node_id{transaction_->root()});
        committed_ = true;
        return result::success;
      }
    }
    return result::success;
  }
  runtime::application& application_;
  gneiss_scene_instance scene_{};
  scene_document& session_;
  std::shared_ptr<const prefab_placement_snapshot> snapshot_;
  std::unique_ptr<prefab_placement> transaction_;
  std::vector<prefab_node_record> records_;
  std::size_t old_cursor_{}, new_cursor_{};
  bool removing_{}, committed_{}, cancelled_{};
  result status_{result::success};
  std::function<gneiss_result(const std::function<gneiss_result()>&)> accept_;
};
using prefab_snapshots = std::vector<std::shared_ptr<const prefab_placement_snapshot>>;
class prefab_refresh_operation final : public editor_history_operation {
public:
  using acceptance = std::function<gneiss_result(const std::function<gneiss_result()>&)>;
  prefab_refresh_operation(runtime::application& application, scene_document& session,
                           gneiss_scene_instance scene, prefab_snapshots snapshots,
                           acceptance accept = {})
      : application_(application), scene_(scene), session_(session),
        snapshots_(std::move(snapshots)), accept_(std::move(accept)),
        selection_(session.selection()) {
    transactions_.resize(snapshots_.size());
    for (const auto& snapshot : snapshots_) {
      uuids_.push_back(snapshot->author.instance_uuid);
    }
    const auto selected =
        std::ranges::find(session.prefab_nodes(), selection_, &prefab_node_record::node);
    if (selected != session.prefab_nodes().end() && is_target(selected->instance_uuid)) {
      selected_instance_ = selected->instance_uuid;
      selected_source_ = selected->source_node_uuid;
      selection_ = {};
    }
    std::size_t capacity = session.prefab_nodes().size();
    for (const auto& snapshot : snapshots_) {
      capacity += snapshot->order.size() + 1U;
    }
    records_.reserve(capacity);
    entries_.reserve(snapshots_.size());
  }
  result advance(bool& complete) noexcept override try {
    complete = false;
    if (committed_ || cancelled_ || status_.failed()) {
      return cleanup(complete);
    }
    if (session_.scene_handle() != scene_) {
      status_ = result::invalid_state;
      return result::success;
    }
    if (construct_cursor_ < transactions_.size()) {
      auto& transaction = transactions_[construct_cursor_];
      if (!transaction) {
        status_ = from_native(prefab_placement::begin(
            application_, scene_, snapshots_[construct_cursor_], false, transaction, true));
        if (status_.failed()) {
          return result::success;
        }
        entries_.push_back(transaction.get());
      }
      bool ready{};
      status_ = from_native(transaction->advance(ready));
      if (status_.ok() && ready) {
        ++construct_cursor_;
      }
      return result::success;
    }
    prepare_mirror();
    return result::success;
  } catch (const std::bad_alloc&) {
    status_ = result::out_of_memory;
    return result::success;
  } catch (...) {
    status_ = result::internal;
    return result::success;
  }
  void cancel() noexcept override {
    if (!committed_) {
      cancelled_ = true;
    }
  }
  [[nodiscard]] bool can_cancel() const noexcept override { return !committed_; }

private:
  [[nodiscard]] bool is_target(std::string_view uuid) const {
    return std::ranges::find(uuids_, uuid) != uuids_.end();
  }
  result cleanup(bool& complete) {
    if (cleanup_cursor_ < transactions_.size()) {
      auto& transaction = transactions_[cleanup_cursor_];
      if (!transaction || transaction->cleanup_step()) {
        transaction.reset();
        ++cleanup_cursor_;
      }
      return result::success;
    }
    for (unsigned step = 0; step < 64U && !records_.empty(); ++step) {
      records_.pop_back();
    }
    complete = records_.empty();
    if (!complete || committed_) {
      return result::success;
    }
    return cancelled_ ? result::invalid_state : status_;
  }
  result append_record() {
    engine::scene::prefab_node_view info;
    const auto result = from_native(transactions_[mirror_cursor_]->preview(node_cursor_++, info));
    if (result.failed()) {
      return result;
    }
    prefab_node_record record;
    const auto converted =
        make_prefab_record(info, snapshots_[mirror_cursor_]->author, session_, record);
    if (converted.failed()) {
      return converted;
    }
    if (record.instance_uuid == selected_instance_) {
      if (record.is_instance_root) {
        selection_ = record.node;
      }
      if (record.source_node_uuid == selected_source_) {
        selection_ = record.node;
      }
    }
    records_.push_back(std::move(record));
    return result::success;
  }
  void prepare_mirror() {
    for (unsigned step = 0; step < 64U; ++step) {
      if (old_cursor_ < session_.prefab_nodes().size()) {
        const auto& record = session_.prefab_nodes()[old_cursor_++];
        if (!is_target(record.instance_uuid)) {
          records_.push_back(record);
        }
      } else if (mirror_cursor_ < transactions_.size()) {
        if (node_cursor_ == transactions_[mirror_cursor_]->node_count()) {
          ++mirror_cursor_;
          node_cursor_ = 0U;
          continue;
        }
        status_ = append_record();
        if (status_.failed()) {
          return;
        }
      } else {
        const auto commit = [&] { return prefab_placement::commit_replacements(entries_); };
        status_ = from_native(accept_ ? accept_(commit) : commit());
        if (status_.ok()) {
          session_.publish_prefab_records(records_, selection_);
          committed_ = true;
        }
        return;
      }
    }
  }
  runtime::application& application_;
  gneiss_scene_instance scene_{};
  scene_document& session_;
  prefab_snapshots snapshots_;
  acceptance accept_;
  scene_node_id selection_;
  std::string selected_instance_, selected_source_;
  std::vector<std::string> uuids_;
  std::vector<std::unique_ptr<prefab_placement>> transactions_;
  std::vector<prefab_placement*> entries_;
  std::vector<prefab_node_record> records_;
  std::size_t construct_cursor_{}, cleanup_cursor_{}, old_cursor_{}, mirror_cursor_{},
      node_cursor_{};
  bool committed_{}, cancelled_{};
  result status_{result::success};
};
class prefab_refresh_command final : public editor_command {
public:
  prefab_refresh_command(runtime::application& application, scene_document& session,
                         gneiss_scene_instance scene, prefab_snapshots before,
                         prefab_snapshots after)
      : application_(application), scene_(scene), session_(session), before_(std::move(before)),
        after_(std::move(after)) {}
  [[nodiscard]] std::string_view label() const noexcept override { return "刷新同源 Prefab 实例"; }
  result undo() noexcept override { return result::not_ready; }
  result redo() noexcept override { return result::not_ready; }
  result
  begin_history_operation(bool undo,
                          std::unique_ptr<editor_history_operation>& output) noexcept override try {
    if (session_.scene_handle() != scene_) {
      return result::invalid_state;
    }
    output = std::make_unique<prefab_refresh_operation>(application_, session_, scene_,
                                                        undo ? before_ : after_);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }

private:
  runtime::application& application_;
  gneiss_scene_instance scene_{};
  scene_document& session_;
  prefab_snapshots before_, after_;
};
class prefab_command final : public editor_command {
public:
  prefab_command(runtime::application& application, scene_document& session,
                 gneiss_scene_instance scene,
                 std::shared_ptr<const prefab_placement_snapshot> snapshot, bool removal = false)
      : application_(application), scene_(scene), session_(session), snapshot_(std::move(snapshot)),
        removal_(removal) {}
  [[nodiscard]] std::string_view label() const noexcept override {
    return removal_ ? "删除 Prefab 实例" : "放置 Prefab 实例";
  }
  result undo() noexcept override { return result::not_ready; }
  result redo() noexcept override { return result::not_ready; }
  result
  begin_history_operation(bool undo,
                          std::unique_ptr<editor_history_operation>& output) noexcept override try {
    if (session_.scene_handle() != scene_) {
      return result::invalid_state;
    }
    output = std::make_unique<prefab_history_operation>(application_, session_, scene_, snapshot_,
                                                        undo != removal_);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }

private:
  runtime::application& application_;
  gneiss_scene_instance scene_{};
  scene_document& session_;
  std::shared_ptr<const prefab_placement_snapshot> snapshot_;
  bool removal_{};
};
}
struct editor_prefab_edit::state {
  explicit state(runtime::application& host) : application(host) {}
  runtime::application& application;
  gneiss_scene_instance scene{};
  scene_document* session{};
  editor_command_history* history{};
  std::unique_ptr<runtime::prefab_preparation> preparation;
  engine::resource::prefab_instance_description author;
  engine::scene::prepared_scene_description prepared;
  std::vector<engine::render::render_asset_lease> assets;
  engine::render::texture_load_completion completion;
  std::unique_ptr<editor_command> command;
  std::unique_ptr<editor_history_operation> operation;
  std::shared_ptr<prefab_placement_snapshot> captured;
  bool removing{}, refreshing{};
  std::vector<std::string> refresh_targets;
  prefab_snapshots before, after;
  std::string target_uuid;
  std::uint64_t request{};
  bool asset_pending{}, cancelled{}, active{}, visible{};
  result status{result::success};
  result failure{result::success};
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
      status = from_native(prefab_placement::capture_step(
          application, refresh_targets[before.size()], scene, captured, complete));
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
      status =
          from_native(prefab_placement::freeze(application, prepared.prefabs.at(author.prefab_uri),
                                               before[after.size()]->author, assets, snapshot));
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
    operation = std::make_unique<prefab_refresh_operation>(
        application, *session, scene, after, [this](const std::function<gneiss_result()>& commit) {
          return completion.versions.empty() ? commit()
                                             : application.accept_edit_assets(completion, commit);
        });
  }

  bool poll_preparation() {

    bool complete{};
    status = from_native(preparation->poll(prepared, complete));
    if (!complete) {
      return false;
    }
    preparation.reset();
    if (status.ok() && !cancelled) {
      std::vector<engine::render::render_asset_reload> missing;
      for (const auto& asset : prepared.assets) {
        engine::render::render_asset_lease lease;
        const auto ready =
            refreshing ? GNEISS_ERROR_NOT_READY : application.query_ready_asset(asset, lease);
        if (ready == GNEISS_SUCCESS) {
          assets.push_back(std::move(lease));
        } else if (ready == GNEISS_ERROR_NOT_READY) {
          missing.push_back(asset);
        } else {
          status = from_native(ready);
          break;
        }
      }
      if (status.ok() && !missing.empty()) {
        status = from_native(application.prepare_edit_assets(missing, 1U, request));
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
    status = from_native(application.poll_assets(completion, ready));
    if (status.ok() && !ready) {
      return false;
    }
    asset_pending = false;
    if (status.ok()) {
      status = from_native(completion.result);
    }
    if (status.ok() && completion.request != request) {
      status = result::invalid_state;
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
result editor_prefab_edit::operation() const noexcept {
  return state_ ? state_->status : result::success;
}
void editor_prefab_edit::dismiss() noexcept {
  if (!active()) {
    state_.reset();
  }
}
result editor_prefab_edit::request(runtime::application& application,
                                   tasks::task_executor& executor, scene_document& session,
                                   editor_command_history& history, std::string_view name,
                                   std::string_view uri, scene_node_id parent) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return result::not_ready;
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
      return result::invalid_handle;
    }
    next->author.parent_uuid = found->uuid;
  }
  status = from_native(
      runtime::prefab_preparation::create(application, executor, uri, next->preparation));
  if (status.failed()) {
    return status;
  }
  next->active = next->visible = true;
  state_ = std::move(next);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result editor_prefab_edit::request_remove(runtime::application& application,
                                          scene_document& session, editor_command_history& history,
                                          scene_node_id root) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return result::not_ready;
  }
  const auto found = std::ranges::find(session.prefab_nodes(), root, &prefab_node_record::node);
  if (found == session.prefab_nodes().end() || !found->is_instance_root) {
    return result::invalid_handle;
  }
  auto next = std::make_unique<state>(application);
  next->session = &session;
  next->history = &history;
  next->scene = session.scene_handle();
  next->target_uuid = found->instance_uuid;
  next->active = next->visible = next->removing = true;
  state_ = std::move(next);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

result editor_prefab_edit::request_refresh(runtime::application& application,
                                           tasks::task_executor& executor, scene_document& session,
                                           editor_command_history& history,
                                           scene_node_id root) try {
  if (active() || history.is_pending() || !session.is_open()) {
    return result::not_ready;
  }
  const auto found = std::ranges::find(session.prefab_nodes(), root, &prefab_node_record::node);
  if (found == session.prefab_nodes().end() || !found->is_instance_root) {
    return result::invalid_handle;
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
  const auto status = from_native(runtime::prefab_preparation::create(
      application, executor, found->prefab_uri, next->preparation));
  if (status.failed()) {
    return status;
  }
  next->active = next->visible = next->refreshing = true;
  state_ = std::move(next);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
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
      value.status = result::invalid_state;
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
      value.status = from_native(prefab_placement::capture_step(
          value.application, value.target_uuid, value.scene, value.captured, complete));
      if (value.status.ok() && !complete) {
        return;
      }
      snapshot = value.captured;
    } else {
      value.status = from_native(prefab_placement::freeze(
          value.application, std::move(value.prepared.prefabs.at(value.author.prefab_uri)),
          value.author, std::move(value.assets), snapshot));
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
        value.application, *value.session, value.scene, std::move(snapshot), value.removing,
        [&value](const std::function<gneiss_result()>& commit) {
          return value.completion.versions.empty()
                     ? commit()
                     : value.application.accept_edit_assets(value.completion, commit);
        });
  } catch (const std::bad_alloc&) {
    value.failure = result::out_of_memory;
    value.status = value.failure;
    cancel();
  } catch (...) {
    value.failure = result::internal;
    value.status = value.failure;
    cancel();
  }
}
}
