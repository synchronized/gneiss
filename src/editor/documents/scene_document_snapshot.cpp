// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/resource/asset_file.hpp"
#include <algorithm>
namespace gneiss::editor {
gneiss::result<gneiss::prefab_asset_ref> scene_object_resources::prefab_source() const {
  auto restored = gneiss::restore_prefab_record(prefab_content);
  if (!restored)
    return restored.error();
  return gneiss::make_ref<const gneiss::prefab_description>(std::move(*restored));
}

gneiss::result<gneiss::scene_description> scene_subtree_snapshot::description() const {
  auto restored = gneiss::restore_scene_record(content);
  if (!restored)
    return restored.error();
  return std::move(*restored);
}

gneiss::result<scene_subtree_snapshot> freeze_prefab_snapshot(
    prefab_header_snapshot source,
    const std::map<std::string, gneiss::render_resource_ref, std::less<>>& assets) {
  scene_subtree_snapshot value;
  auto author = std::move(source.author);
  if (!source.source || !author.prefab)
    return gneiss::error::invalid_argument;
  // 来源冲突与实例化使用同一回放规则，资源取自成功应用后的有效描述。
  auto replayed = gneiss::replay_prefab_reference(*source.source, *author.prefab);
  if (!replayed)
    return replayed.error();
  const auto& effective = replayed->description;
  auto status = gneiss::error::success;
  value.root_uuid = author.uuid;
  value.parent = author.parent;
  author.parent = {};
  gneiss::scene_description description;
  description.uuid = author.uuid;
  description.objects.push_back(std::move(author));
  auto content = gneiss::capture_scene_record(description);
  if (!content)
    return content.error();
  value.content = std::move(*content);
  auto& binding = value.resources[value.root_uuid];
  auto frozen_source = gneiss::capture_prefab_record(*source.source);
  if (!frozen_source)
    return frozen_source.error();
  binding.prefab_content = std::move(*frozen_source);
  binding.source_asset = std::move(source.source_asset);
  binding.source_nodes.resize(source.source->objects.size());
  const auto find = [&](std::string_view uri, gneiss::render_resource_kind type,
                        gneiss::render_resource_ref& result) {
    const auto found = assets.find(uri);
    if (found == assets.end())
      return gneiss::error::not_ready;
    if (!found->second || gneiss::resource_kind(found->second) != type)
      return gneiss::error::invalid_argument;
    result = found->second;
    return gneiss::error::success;
  };
  for (std::size_t i = 0; i < binding.source_nodes.size(); ++i) {
    const auto* renderer =
        effective.objects[i].components.get<gneiss::mesh_component_description>();
    if (!renderer)
      continue;
    gneiss::render_resource_ref mesh, material;
    status = find(renderer->mesh_uri, gneiss::render_resource_kind::mesh, mesh);
    if (status.failed())
      return status;
    status = find(renderer->material_uri, gneiss::render_resource_kind::material, material);
    if (status.failed())
      return status;
    binding.source_nodes[i] = {gneiss::resource_cast<gneiss::mesh>(mesh),
                               gneiss::resource_cast<gneiss::material>(material)};
  }
  return value;
}

gneiss::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const gneiss::scene_content& source,
                             gneiss::ref<const gneiss::scene> asset) {
  if (!source.description)
    return gneiss::error::invalid_argument;
  scene_document_snapshot snapshot;
  snapshot.uri = std::move(uri);
  auto content = gneiss::capture_scene_record(*source.description);
  if (!content)
    return content.error();
  snapshot.content = std::move(*content);
  snapshot.source_asset = std::move(asset);
  for (const auto& [path, prefab] : source.prefabs) {
    if (!prefab.description)
      return gneiss::error::invalid_argument;
    auto content = gneiss::capture_prefab_record(*prefab.description);
    if (!content)
      return content.error();
    snapshot.prefabs.emplace(path, prefab_document_snapshot{std::move(*content), prefab.resource});
  }
  return snapshot;
}
gneiss::result<gneiss::scene_content>
make_scene_document_inputs(const scene_document_snapshot& snapshot) {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  gneiss::scene_content inputs;
  inputs.description = gneiss::make_ref<const gneiss::scene_description>(std::move(*description));
  for (const auto& [uri, prefab] : snapshot.prefabs) {
    auto description = gneiss::restore_prefab_record(prefab.content);
    if (!description)
      return description.error();
    inputs.prefabs.emplace(
        uri, gneiss::scene_prefab_dependency{
                 prefab.source_asset,
                 gneiss::make_ref<const gneiss::prefab_description>(std::move(*description)), 0});
  }
  return inputs;
}
gneiss::result<gneiss::scene_description> scene_document_snapshot::description() const {
  auto restored = gneiss::restore_scene_record(content);
  if (!restored)
    return restored.error();
  return std::move(*restored);
}
gneiss::result<gneiss::composed_scene>
compose_scene_document(const scene_document_snapshot& snapshot) {
  auto description = snapshot.description();
  if (!description)
    return description.error();
  std::map<std::string, gneiss::prefab_description> sources;
  for (const auto& object : description->objects) {
    if (!object.prefab || sources.contains(object.prefab->uri))
      continue;
    auto found = snapshot.prefabs.find(object.prefab->uri);
    if (found == snapshot.prefabs.end())
      return gneiss::error::not_found;
    auto restored = gneiss::restore_prefab_record(found->second.content);
    if (!restored)
      return restored.error();
    sources.emplace(object.prefab->uri, std::move(*restored));
  }
  std::map<std::string, const gneiss::prefab_description*, std::less<>> borrowed;
  for (const auto& [uri, source] : sources)
    borrowed.emplace(uri, &source);
  return gneiss::compose_scene(*description, borrowed);
}
gneiss::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot& before,
                       std::span<const author_document_change> changes) {
  std::vector<gneiss::render_resource_source> pending;
  auto restored = make_scene_document_inputs(before);
  if (!restored)
    return restored.error();
  auto inputs = std::move(*restored);
  auto scene = inputs.description;
  for (const auto& change : changes) {
    const auto uri = "asset://" + change.path;
    if (uri == before.uri) {
      if (!change.replacement)
        return gneiss::error::invalid_argument;
      auto decoded = gneiss::asset_file::deserialize_ref<gneiss::scene_description>(
          std::as_bytes(std::span(change.replacement->data(), change.replacement->size())));
      if (!decoded)
        return decoded.error().error;
      scene = std::move(*decoded);
    } else if (uri.ends_with(".prefab.json")) {
      if (!change.replacement) {
        inputs.prefabs.erase(uri);
        continue;
      }
      auto decoded = gneiss::asset_file::deserialize_ref<gneiss::prefab_description>(
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
  auto dependencies = gneiss::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  for (const auto& asset : *dependencies) {
    const auto found = before.assets.find(asset.uri);
    if (found == before.assets.end() || gneiss::resource_kind(found->second) != asset.type) {
      pending.push_back(asset);
      continue;
    }
    next->assets.emplace(asset.uri, found->second);
  }
  return prepared_scene_document{std::move(*next), std::move(pending)};
}

gneiss::result<std::vector<gneiss::object_reference_issue>>
scene_document_snapshot::reference_issues() const {
  std::vector<gneiss::document_object_view> documents{{content.get().uuid, content.get().objects}};
  for (const auto& [uri, source] : prefabs)
    documents.push_back({source.content.get().uuid, source.content.get().objects});
  std::vector<gneiss::object_reference_issue> result;
  for (const auto& document : documents) {
    auto issues = gneiss::inspect_object_references(document, documents);
    if (!issues)
      return issues.error();
    result.insert(result.end(), issues->begin(), issues->end());
  }
  return result;
}
}
