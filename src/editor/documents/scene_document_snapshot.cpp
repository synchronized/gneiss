// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/resource/asset_file.hpp"
#include <algorithm>
namespace gneiss::editor {
engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri,
                             const engine::scene_instantiation_data& source) noexcept try {
  scene_document_snapshot snapshot;
  snapshot.uri = std::move(uri);
  auto content = engine::capture_scene_record(source.description);
  if (!content)
    return content.error();
  snapshot.content = std::move(*content);
  snapshot.source_asset = source.source_asset;
  for (const auto& [path, prefab] : source.prefabs) {
    if (!prefab.resource)
      return engine::error::invalid_argument;
    auto content = engine::capture_prefab_record(*prefab.resource);
    if (!content)
      return content.error();
    snapshot.prefabs.emplace(path,
                             prefab_document_snapshot{std::move(*content), prefab.source_asset});
  }
  return snapshot;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<engine::scene_preparation_inputs>
make_scene_document_inputs(const scene_document_snapshot& snapshot) noexcept try {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  engine::scene_preparation_inputs inputs;
  inputs.scene = engine::make_ref<const engine::scene_description>(std::move(*description));
  inputs.source_asset = snapshot.source_asset;
  for (const auto& [uri, prefab] : snapshot.prefabs) {
    auto description = engine::restore_prefab_record(prefab.content);
    if (!description)
      return description.error();
    inputs.prefabs.emplace(
        uri, engine::make_ref<const engine::prefab_description>(std::move(*description)));
    inputs.prefab_assets.emplace(uri, prefab.source_asset);
  }
  return inputs;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<engine::scene_description> scene_document_snapshot::description() const noexcept
    try {
  auto restored = engine::restore_scene_record(content);
  if (!restored)
    return restored.error();
  return std::move(*restored);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<composed_scene_document>
compose_scene_document(const scene_document_snapshot& snapshot) noexcept try {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  std::map<std::string, engine::prefab_description> sources;
  for (const auto& object : description->objects) {
    if (!object.prefab || sources.contains(object.prefab->uri))
      continue;
    auto found = snapshot.prefabs.find(object.prefab->uri);
    if (found == snapshot.prefabs.end())
      return engine::error::not_found;
    auto restored = engine::restore_prefab_record(found->second.content);
    if (!restored)
      return restored.error();
    sources.emplace(object.prefab->uri, std::move(*restored));
  }
  const auto address_of = [&](engine::guid uuid) -> document_object_address {
    auto object =
        std::ranges::find(description->objects, uuid, &engine::game_object_description::uuid);
    return {uuid, object != description->objects.end() && object->prefab
                      ? sources.at(object->prefab->uri).root_uuid
                      : engine::guid{}};
  };
  composed_scene_document result;
  for (const auto& object : description->objects) {
    if (!object.prefab) {
      composed_document_object entry;
      entry.address = {object.uuid, {}};
      entry.parent = address_of(object.parent_uuid);
      entry.effective = object;
      result.objects.push_back(std::move(entry));
      continue;
    }
    const auto& source = sources.at(object.prefab->uri);
    auto replayed = engine::replay_prefab_reference(source, *object.prefab);
    if (!replayed)
      return replayed.error();
    if (!replayed->conflicts.empty())
      result.conflicts.emplace(object.uuid, std::move(replayed->conflicts));
    for (auto& member : replayed->description.objects) {
      const bool root = member.uuid == source.root_uuid;
      auto original =
          std::ranges::find(source.objects, member.uuid, &engine::game_object_description::uuid);
      if (original == source.objects.end())
        return engine::error::not_found;
      composed_document_object entry;
      entry.address = {object.uuid, member.uuid};
      entry.parent = root ? address_of(object.parent_uuid)
                          : document_object_address{object.uuid, member.parent_uuid};
      entry.prefab_uri = object.prefab->uri;
      entry.source_transform = original->transform;
      entry.instance_root = root;
      for (const auto& change : object.prefab->overrides)
        if (change.key.target.source_node_uuid == member.uuid)
          entry.overrides.push_back(change.key);
      if (root) {
        member.name = object.name;
        member.transform = object.transform;
      }
      entry.effective = std::move(member);
      result.objects.push_back(std::move(entry));
    }
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot& before,
                       std::span<const author_document_change> changes) noexcept try {
  std::vector<engine::render_resource_source> pending;
  auto restored = make_scene_document_inputs(before);
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
  auto next = make_scene_document_snapshot(before.uri, prepared);
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

engine::result<std::vector<engine::object_reference_issue>>
scene_document_snapshot::reference_issues() const noexcept try {
  std::vector<engine::document_object_view> documents{{content.get().uuid, content.get().objects}};
  for (const auto& [uri, source] : prefabs)
    documents.push_back({source.content.get().uuid, source.content.get().objects});
  std::vector<engine::object_reference_issue> result;
  for (const auto& document : documents) {
    auto issues = engine::inspect_object_references(document, documents);
    if (!issues)
      return issues.error();
    result.insert(result.end(), issues->begin(), issues->end());
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
