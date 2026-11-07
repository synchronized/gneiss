// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime/application.hpp"
namespace gneiss::editor {
namespace {
engine::result<scene_document_snapshot> freeze(std::string uri,
                                               const engine::scene_instantiation_data& source) {
  scene_document_snapshot snapshot;
  snapshot.uri = std::move(uri);
  auto content = engine::serialization::capture_value(source.description);
  if (!content)
    return content.error().error;
  snapshot.content = std::move(*content);
  snapshot.source_asset = source.source_asset;
  for (const auto& [path, prefab] : source.prefabs) {
    if (!prefab.resource)
      return engine::error::invalid_argument;
    auto content = engine::serialization::capture_value(*prefab.resource);
    if (!content)
      return content.error().error;
    snapshot.prefabs.emplace(path,
                             prefab_document_snapshot{std::move(*content), prefab.source_asset});
  }
  return snapshot;
}
engine::result<engine::scene_preparation_inputs>
inputs_from(const scene_document_snapshot& snapshot) {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  engine::scene_preparation_inputs inputs;
  inputs.scene = engine::make_ref<const engine::scene_description>(std::move(*description));
  inputs.source_asset = snapshot.source_asset;
  for (const auto& [uri, prefab] : snapshot.prefabs) {
    auto description =
        engine::serialization::restore_value<engine::prefab_description>(prefab.content);
    if (!description)
      return description.error().error;
    inputs.prefabs.emplace(
        uri, engine::make_ref<const engine::prefab_description>(std::move(*description)));
    inputs.prefab_assets.emplace(uri, prefab.source_asset);
  }
  return inputs;
}
}
engine::result<engine::scene_description> scene_document_snapshot::description() const noexcept
    try {
  auto restored = engine::serialization::restore_value<engine::scene_description>(content);
  if (!restored)
    return restored.error().error;
  return std::move(*restored);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<scene_document_snapshot>
capture_scene_document(runtime::application& app, const scene_document& document) noexcept try {
  if (!document.is_open() || !app.is_owner_thread() || !app.accepts_requests())
    return engine::error::invalid_state;
  auto captured = app.scenes()->capture_prepared(document.scene_handle());
  if (!captured)
    return captured.error();
  auto next = freeze(std::string(document.uri()), captured->description);
  if (!next)
    return next.error();
  next->assets = std::move(captured->assets);
  return next;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot& before,
                       std::span<const author_document_change> changes) noexcept try {
  std::vector<engine::render_resource_source> pending;
  auto restored = inputs_from(before);
  if (!restored)
    return restored.error();
  auto inputs = std::move(*restored);
  auto scene = inputs.scene;
  engine::scene_diagnostic diagnostic;
  for (const auto& change : changes) {
    const auto uri = "asset://" + change.path;
    if (uri == before.uri) {
      if (!change.replacement)
        return engine::error::invalid_argument;
      auto decoded = engine::asset_file::deserialize_ref<engine::scene_description>(
          std::as_bytes(std::span(change.replacement->data(), change.replacement->size())));
      if (!decoded)
        return decoded.error().error;
      scene = std::move(*decoded);
    } else if (uri.ends_with(".prefab.json")) {
      if (!change.replacement) {
        inputs.prefabs.erase(uri);
        inputs.prefab_assets.erase(uri);
        continue;
      }
      auto decoded = engine::asset_file::deserialize_ref<engine::prefab_description>(
          std::as_bytes(std::span(change.replacement->data(), change.replacement->size())));
      if (!decoded)
        return decoded.error().error;
      inputs.prefabs.insert_or_assign(uri, std::move(*decoded));
    }
  }
  inputs.scene = std::move(scene);
  engine::scene_instantiation_data prepared;
  auto status = engine::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  auto next = freeze(before.uri, prepared);
  if (!next)
    return next.error();
  for (const auto& asset : prepared.assets) {
    const auto found = before.assets.find(asset.uri);
    if (found == before.assets.end() || engine::resource_kind(found->second) != asset.type) {
      pending.push_back(asset);
      continue;
    }
    next->assets.emplace(asset.uri, found->second);
  }
  return prepared_scene_document{std::move(*next), std::move(pending)};
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error restore_scene_document(runtime::application& app, scene_document& document,
                                     const scene_document_snapshot& snapshot) noexcept try {
  if (!app.is_owner_thread() || !app.accepts_requests() || !document.is_open() ||
      document.uri() != snapshot.uri)
    return engine::error::invalid_state;
  scene_instance_id instance;
  auto inputs = inputs_from(snapshot);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto prepared_status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (prepared_status.failed())
    return prepared_status;
  auto status_value = app.scenes()->instantiate_prepared(std::move(prepared), snapshot.assets);
  if (!status_value)
    return status_value.error();
  instance = std::move(*status_value);
  auto status = document.adopt(app, app.native_world(), instance, snapshot.uri);
  if (status.failed())
    (void)app.scenes()->unload(instance);
  return status;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::error scene_document_edit::finish(engine::error status) noexcept {
  if (request_ && application_) {
    (void)application_->discard_prepared_assets(request_);
    request_ = 0;
  }
  before_.reset();
  after_.reset();
  selection_ = {};
  return status_ = status;
}
void scene_document_edit::cancel() noexcept {
  if (status_ == engine::error::not_ready)
    (void)finish(engine::error::cancelled);
}
engine::error scene_document_edit::begin(runtime::application& app, scene_document& document,
                                         std::span<const author_document_change> changes,
                                         std::string label,
                                         std::function<void(bool)> selection) noexcept try {
  using engine::error;
  if (status_ == error::not_ready)
    return error::not_ready;
  (void)finish(error::invalid_state);
  auto captured = capture_scene_document(app, document);
  if (!captured)
    return finish(captured.error());
  before_ = std::make_shared<scene_document_snapshot>(std::move(*captured));
  auto prepared = prepare_scene_document(*before_, changes);
  if (!prepared)
    return finish(prepared.error());
  after_ = std::make_shared<scene_document_snapshot>(std::move(prepared->snapshot));
  scene_ = document.scene_handle();

  application_ = &app;
  if (!prepared->missing.empty()) {
    auto loading = app.prepare_assets(prepared->missing, 1);
    if (!loading)
      return finish(loading.error());
    request_ = loading->request;
    for (auto& asset : loading->assets)
      after_->assets.insert_or_assign(std::string(engine::resource_uri(asset)), std::move(asset));
  }
  document_ = &document;
  label_ = std::move(label);
  selection_ = std::move(selection);
  status_ = error::not_ready;
  return error::success;
} catch (const std::bad_alloc&) {
  return finish(engine::error::out_of_memory);
} catch (...) {
  return finish(engine::error::internal);
}
engine::error scene_document_edit::advance(runtime::application& app, scene_document& document,
                                           editor_command_history& history) noexcept try {
  using engine::error;
  if (status_ != error::not_ready)
    return status_;
  if (&app != application_ || &document != document_ || !app.is_owner_thread() ||
      !app.accepts_requests() || !document.is_open() || document.scene_handle() != scene_)
    return finish(error::invalid_state);
  if (history.is_pending())
    return finish(error::invalid_state);
  if (request_) {
    auto completed = app.poll_prepared_assets(request_);
    if (!completed) {
      if (completed.error() == error::not_ready)
        return error::not_ready;
      return finish(completed.error());
    }
    request_ = 0;
    if (completed->result.failed())
      return finish(completed->result);
    for (auto& asset : completed->assets)
      after_->assets.insert_or_assign(std::string(engine::resource_uri(asset)), std::move(asset));
  }
  // 比较持久化值树，不通过 JSON 编码判断等待期间的修改。
  auto current = capture_scene_document(app, document);
  if (!current)
    return finish(current.error());
  if (current->content != before_->content || current->prefabs != before_->prefabs)
    return finish(error::invalid_state);
  auto status = error{error::success};
  const auto apply = [&app, &document, selection = selection_](
                         const std::shared_ptr<scene_document_snapshot>& snapshot, bool undo) {
    auto result = restore_scene_document(app, document, *snapshot);
    if (result.ok()) {
      document.mark_dirty();
      if (selection)
        selection(undo);
    }
    return result;
  };
  const bool was_dirty = document.is_dirty();
  status = history.execute({.label = label_,
                            .undo = [apply, before = before_] { return apply(before, true); },
                            .redo = [apply, after = after_] { return apply(after, false); },
                            .merge_key = {}});
  if (status.failed() && !was_dirty)
    document.clear_dirty();
  return finish(status);
} catch (const std::bad_alloc&) {
  return finish(engine::error::out_of_memory);
} catch (...) {
  return finish(engine::error::internal);
}
}
