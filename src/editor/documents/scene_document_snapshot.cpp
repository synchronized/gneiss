// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/meta/serialization.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/scene_service.hpp"
namespace gneiss::editor {
engine::result<scene_document_snapshot>
capture_scene_document(runtime::application& app, const scene_document& document) noexcept try {
  if (!document.is_open() || !app.is_owner_thread() || !app.accepts_requests())
    return engine::error::invalid_state;
  scene_document_snapshot next;
  next.uri = document.uri();
  auto status_value = app.scenes()->capture_prepared(document.scene_handle());
  if (!status_value)
    return status_value.error();
  next.description = std::move(status_value->description);
  next.assets = std::move(status_value->assets);

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
  auto scene = std::make_shared<engine::scene_description>(before.description.description);
  engine::scene_preparation_inputs inputs;
  for (const auto& [uri, prefab] : before.description.prefabs)
    inputs.prefabs.emplace(uri, prefab.resource);
  const auto decode = [&](const std::string& json, auto& output) {
    auto decoded_result = engine::meta::deserialize<std::remove_cvref_t<decltype(output)>>(
        std::as_bytes(std::span(json.data(), json.size())));
    if (!decoded_result) {
      return decoded_result.error().error;
    }
    output = std::move(*decoded_result);
    return gneiss::engine::error::success;
  };
  engine::scene_diagnostic diagnostic;
  for (const auto& change : changes) {
    const auto uri = "asset://" + change.path;
    if (uri == before.uri) {
      if (!change.replacement)
        return engine::error::invalid_argument;
      const auto status = decode(*change.replacement, *scene);
      if (status.failed())
        return status;
    } else if (uri.ends_with(".prefab.json")) {
      if (!change.replacement) {
        inputs.prefabs.erase(uri);
        continue;
      }
      auto prefab = std::make_shared<engine::prefab_description>();
      const auto status = decode(*change.replacement, *prefab);
      if (status.failed())
        return status;
      inputs.prefabs.insert_or_assign(uri, engine::prefab_asset_ref{std::move(prefab)});
    }
  }
  inputs.scene = engine::asset_ref<const engine::scene_description>{std::move(scene)};
  scene_document_snapshot next;
  next.uri = before.uri;
  auto status = engine::prepare_loaded_scene(inputs, {}, next.description, diagnostic, {});
  if (status.failed())
    return status;
  for (const auto& asset : next.description.assets) {
    const auto found = before.assets.find(asset.uri);
    if (found == before.assets.end() || engine::resource_kind(found->second) != asset.type) {
      pending.push_back(asset);
      continue;
    }
    next.assets.emplace(asset.uri, found->second);
  }
  return prepared_scene_document{std::move(next), std::move(pending)};
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
  auto status_value = app.scenes()->instantiate_prepared(snapshot.description, snapshot.assets);
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
    (void)application_->scene_loading_service().discard_prepared_assets(request_);
    request_ = 0;
  }
  before_.reset();
  after_.reset();
  selection_ = {};
  baseline_.clear();
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
  auto serialize_result = app.scenes()->serialize(scene_);
  if (!serialize_result)
    return finish(serialize_result.error());

  baseline_ = std::move(*serialize_result);

  application_ = &app;
  if (!prepared->missing.empty()) {
    auto loading = app.scene_loading_service().prepare_assets(prepared->missing, 1);
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
  auto serialize_result = app.scenes()->serialize(scene_);
  if (!serialize_result)
    return finish(serialize_result.error());

  const auto& current = *serialize_result;

  auto status = gneiss::engine::error::success;
  if (current != baseline_ || history.is_pending())
    return finish(error::invalid_state);
  if (request_) {
    auto completed = app.scene_loading_service().poll_prepared_assets(request_);
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
