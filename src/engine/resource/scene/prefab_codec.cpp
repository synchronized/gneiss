// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/detail/description_codec.hpp"
#include <algorithm>
#include <cstring>
#include <new>
namespace gneiss::engine {
result<prefab_description, scene_diagnostic>
parse_prefab_description(std::string_view json) noexcept try {
  auto parsed = detail::parse_object_document(json, true);
  if (!parsed)
    return std::move(parsed).error();
  prefab_description prefab;
  prefab.uuid = parsed->uuid;
  prefab.root_uuid = *parsed->root_uuid;
  prefab.source_schema_version = 1U;
  prefab.objects = std::move(parsed->objects);
  prefab.author_json = json;
  for (const auto& object : prefab.objects) {
    if (const auto* mesh = object.components.get<mesh_renderer_description>()) {
      prefab.dependencies.push_back(mesh->mesh_uri);
      prefab.dependencies.push_back(mesh->material_uri);
    }
  }
  std::ranges::sort(prefab.dependencies);
  const auto unique_end = std::ranges::unique(prefab.dependencies).begin();
  prefab.dependencies.erase(unique_end, prefab.dependencies.end());
  return prefab;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}

result<std::string> serialize_prefab_description(const prefab_description& prefab) noexcept {
  if (prefab.author_json.empty() || !prefab.uuid)
    return error::invalid_state;
  return detail::serialize_object_document(prefab.uuid, prefab.root_uuid, prefab.objects,
                                           prefab.author_json);
}
}
namespace gneiss::engine {
result<prefab_description, meta::serialization_diagnostic>
decode_prefab(std::span<const std::byte> bytes) noexcept try {
  if (bytes.size() > prefab_description::maximum_source_bytes)
    return meta::serialization_diagnostic{.message = "资源描述超过单文件大小上限",
                                          .error = error::invalid_argument};
  auto decoded =
      parse_prefab_description({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  if (!decoded) {
    auto failure = std::move(decoded).error();
    return meta::serialization_diagnostic{failure.byte_offset, std::move(failure.path),
                                          std::move(failure.message), failure.error};
  }
  return std::move(*decoded);
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_prefab(const prefab_description& object) noexcept try {
  auto text = serialize_prefab_description(object);
  if (!text)
    return meta::serialization_diagnostic{.error = text.error()};
  if (text->size() > prefab_description::maximum_source_bytes)
    return meta::serialization_diagnostic{.message = "资源描述超过单文件大小上限",
                                          .error = error::invalid_argument};
  std::vector<std::byte> bytes(text->size());
  std::memcpy(bytes.data(), text->data(), text->size());
  return bytes;
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}
