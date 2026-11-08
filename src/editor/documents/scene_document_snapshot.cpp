// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/resource/asset_file.hpp"
#include <algorithm>
namespace gneiss::editor {
engine::result<engine::scene_subtree_snapshot> freeze_prefab_snapshot(
    prefab_header_snapshot source,
    const std::map<std::string, engine::render_resource_ref, std::less<>>& assets) noexcept try {
  engine::scene_subtree_snapshot value;
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
  value.parent_uuid = author.parent_uuid;
  author.parent_uuid = {};
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
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

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
engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const engine::scene_content& content,
                             engine::ref<const engine::scene> source) noexcept try {
  if (!content.description)
    return engine::error::invalid_argument;
  engine::scene_instantiation_data input;
  input.description = *content.description;
  input.source_asset = std::move(source);
  for (const auto& [path, dependency] : content.prefabs)
    input.prefabs.emplace(path,
                          engine::prefab_instantiation_data{.resource = dependency.description,
                                                            .source_asset = dependency.resource,
                                                            .maximum_depth = 0,
                                                            .parent_first = {}});
  return make_scene_document_snapshot(std::move(uri), input);
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
engine::result<engine::composed_scene>
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
  std::map<std::string, const engine::prefab_description*, std::less<>> borrowed;
  for (const auto& [uri, source] : sources)
    borrowed.emplace(uri, &source);
  return engine::compose_scene(*description, borrowed);
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
