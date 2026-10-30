// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/meta/serialization.hpp"
#include "runtime/application.hpp"
namespace gneiss::editor {
engine::error capture_scene_document(runtime::application& app, const scene_document& document,
                                     scene_document_snapshot& output) noexcept try {
  if (!document.is_open() || !app.is_owner_thread() || !app.accepts_requests())
    return engine::error::invalid_state;
  scene_document_snapshot next;
  next.uri = document.uri();
  const auto status =
      app.scenes()->capture_prepared(document.scene_handle(), next.description, next.assets);
  if (status.failed())
    return status;
  output = std::move(next);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error prepare_scene_document(const scene_document_snapshot& before,
                                     std::span<const author_document_change> changes,
                                     scene_document_snapshot& output,
                                     std::vector<engine::asset_source>* missing) noexcept try {
  if (missing)
    missing->clear();
  std::vector<engine::asset_source> pending;
  auto scene = std::make_shared<engine::scene_description>(before.description.description);
  engine::scene_description_inputs inputs;
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
    if (found == before.assets.end() || engine::asset_kind(found->second) != asset.type) {
      if (!missing)
        return engine::error::not_ready;
      pending.push_back(asset);
      continue;
    }
    next.assets.emplace(asset.uri, found->second);
  }
  output = std::move(next);
  if (missing)
    *missing = std::move(pending);
  return engine::error::success;
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
  auto status = app.scenes()->instantiate_prepared(snapshot.description, snapshot.assets, instance);
  if (status.failed())
    return status;
  status = document.adopt(app, app.native_world(), instance, snapshot.uri);
  if (status.failed())
    (void)app.scenes()->unload(instance);
  return status;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
