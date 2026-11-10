// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/resource/asset_file.hpp"
#include <algorithm>
namespace gneiss::editor {
engine::result<engine::prefab_asset_ref> scene_object_resources::prefab_source() const {
  auto restored = engine::restore_prefab_record(prefab_content);
  if (!restored)
    return restored.error();
  return engine::make_ref<const engine::prefab_description>(std::move(*restored));
}

engine::result<engine::scene_description> scene_subtree_snapshot::description() const {
  auto restored = engine::restore_scene_record(content);
  if (!restored)
    return restored.error();
  return std::move(*restored);
}

engine::result<scene_subtree_snapshot> freeze_prefab_snapshot(
    prefab_header_snapshot source,
    const std::map<std::string, engine::render_resource_ref, std::less<>>& assets) {
  scene_subtree_snapshot value;
  auto author = std::move(source.author);
  if (!source.source || !author.prefab)
    return engine::error::invalid_argument;
  // 来源冲突与实例化使用同一回放规则，资源取自成功应用后的有效描述。
  auto replayed = engine::replay_prefab_reference(*source.source, *author.prefab);
  if (!replayed)
    return replayed.error();
  const auto& effective = replayed->description;
  auto status = engine::error::success;
  value.root_uuid = author.uuid;
  value.parent = author.parent;
  author.parent = {};
  engine::scene_description description;
  description.uuid = author.uuid;
  description.objects.push_back(std::move(author));
  auto content = engine::capture_scene_record(description);
  if (!content)
    return content.error();
  value.content = std::move(*content);
  auto& binding = value.resources[value.root_uuid];
  auto frozen_source = engine::capture_prefab_record(*source.source);
  if (!frozen_source)
    return frozen_source.error();
  binding.prefab_content = std::move(*frozen_source);
  binding.source_asset = std::move(source.source_asset);
  binding.source_nodes.resize(source.source->objects.size());
  const auto find = [&](std::string_view uri, engine::render_resource_kind type,
                        engine::render_resource_ref& result) {
    const auto found = assets.find(uri);
    if (found == assets.end())
      return engine::error::not_ready;
    if (!found->second || engine::resource_kind(found->second) != type)
      return engine::error::invalid_argument;
    result = found->second;
    return engine::error::success;
  };
  for (std::size_t i = 0; i < binding.source_nodes.size(); ++i) {
    const auto* renderer = effective.objects[i].components.get<engine::mesh_renderer_description>();
    if (!renderer)
      continue;
    engine::render_resource_ref mesh, material;
    status = find(renderer->mesh_uri, engine::render_resource_kind::mesh, mesh);
    if (status.failed())
      return status;
    status = find(renderer->material_uri, engine::render_resource_kind::material, material);
    if (status.failed())
      return status;
    binding.source_nodes[i] = {engine::resource_cast<gneiss::engine::mesh>(mesh),
                               engine::resource_cast<gneiss::engine::material>(material)};
  }
  return value;
}

engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const engine::scene_content& source,
                             engine::ref<const engine::scene> asset) {
  if (!source.description)
    return engine::error::invalid_argument;
  scene_document_snapshot snapshot;
  snapshot.uri = std::move(uri);
  auto content = engine::capture_scene_record(*source.description);
  if (!content)
    return content.error();
  snapshot.content = std::move(*content);
  snapshot.source_asset = std::move(asset);
  for (const auto& [path, prefab] : source.prefabs) {
    if (!prefab.description)
      return engine::error::invalid_argument;
    auto content = engine::capture_prefab_record(*prefab.description);
    if (!content)
      return content.error();
    snapshot.prefabs.emplace(path, prefab_document_snapshot{std::move(*content), prefab.resource});
  }
  return snapshot;
}
engine::result<engine::scene_content>
make_scene_document_inputs(const scene_document_snapshot& snapshot) {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  engine::scene_content inputs;
  inputs.description = engine::make_ref<const engine::scene_description>(std::move(*description));
  for (const auto& [uri, prefab] : snapshot.prefabs) {
    auto description = engine::restore_prefab_record(prefab.content);
    if (!description)
      return description.error();
    inputs.prefabs.emplace(
        uri, engine::scene_prefab_dependency{
                 prefab.source_asset,
                 engine::make_ref<const engine::prefab_description>(std::move(*description)), 0});
  }
  return inputs;
}
engine::result<engine::scene_description> scene_document_snapshot::description() const {
  auto restored = engine::restore_scene_record(content);
  if (!restored)
    return restored.error();
  return std::move(*restored);
}
engine::result<engine::composed_scene>
compose_scene_document(const scene_document_snapshot& snapshot) {
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
  std::map<std::string, const engine::prefab_description*, std::less<>> borrowed;
  for (const auto& [uri, source] : sources)
    borrowed.emplace(uri, &source);
  return engine::compose_scene(*description, borrowed);
}
engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot& before,
                       std::span<const author_document_change> changes) {
  std::vector<engine::render_resource_source> pending;
  auto restored = make_scene_document_inputs(before);
  if (!restored)
    return restored.error();
  auto inputs = std::move(*restored);
  auto scene = inputs.description;
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
        continue;
      }
      auto decoded = engine::asset_file::deserialize_ref<engine::prefab_description>(
          std::as_bytes(std::span(change.replacement->data(), change.replacement->size())));
      if (!decoded)
        return decoded.error().error;
      inputs.prefabs[uri].description = std::move(*decoded);
    }
  }
  inputs.description = std::move(scene);
  auto next = make_scene_document_snapshot(before.uri, inputs, before.source_asset);
  if (!next)
    return next.error();
  auto composed = compose_scene_document(*next);
  if (!composed)
    return composed.error();
  auto dependencies = engine::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  for (const auto& asset : *dependencies) {
    const auto found = before.assets.find(asset.uri);
    if (found == before.assets.end() || engine::resource_kind(found->second) != asset.type) {
      pending.push_back(asset);
      continue;
    }
    next->assets.emplace(asset.uri, found->second);
  }
  return prepared_scene_document{std::move(*next), std::move(pending)};
}

engine::result<std::vector<engine::object_reference_issue>>
scene_document_snapshot::reference_issues() const {
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
}
}
