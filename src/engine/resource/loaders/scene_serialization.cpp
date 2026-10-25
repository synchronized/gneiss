// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_serialization.hpp"
#include "engine/core/reflection/type_builder.hpp"
#include <cstring>
namespace gneiss::engine {
result decode_scene(std::span<const std::byte> bytes, scene_description& output,
                    serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  if (bytes.size() > scene_description::maximum_source_bytes) {
    diagnostic.message = "资源描述超过单文件大小上限";
    return result::invalid_argument;
  }
  scene_description pending;
  scene_diagnostic detail;
  auto status = parse_scene_description({reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                                        pending, detail);
  diagnostic = {detail.byte_offset, std::move(detail.path), std::move(detail.message)};
  if (status == result::success)
    output = std::move(pending);
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result encode_scene(const scene_description& object, std::vector<std::byte>& output,
                    serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  std::string text;
  auto status = serialize_scene_description(object, text);
  if (status.failed())
    return status;
  std::vector<std::byte> pending(text.size());
  std::memcpy(pending.data(), text.data(), text.size());
  scene_description checked;
  status = decode_scene(pending, checked, diagnostic);
  if (status == result::success)
    output = std::move(pending);
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result register_scene_reflection(const registry_ref& registry) noexcept {
  return type_builder<scene_description>{registry}
      .serialization<&encode_scene, &decode_scene>()
      .commit();
}
result decode_prefab(std::span<const std::byte> bytes, prefab_description& output,
                     serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  if (bytes.size() > prefab_description::maximum_source_bytes) {
    diagnostic.message = "资源描述超过单文件大小上限";
    return result::invalid_argument;
  }
  prefab_description pending;
  scene_diagnostic detail;
  auto status = parse_prefab_description(
      {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, pending, detail);
  diagnostic = {detail.byte_offset, std::move(detail.path), std::move(detail.message)};
  if (status == result::success)
    output = std::move(pending);
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result encode_prefab(const prefab_description& object, std::vector<std::byte>& output,
                     serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  std::string text;
  auto status = serialize_prefab_description(object, text);
  if (status.failed())
    return status;
  std::vector<std::byte> pending(text.size());
  std::memcpy(pending.data(), text.data(), text.size());
  prefab_description checked;
  status = decode_prefab(pending, checked, diagnostic);
  if (status == result::success)
    output = std::move(pending);
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result register_prefab_reflection(const registry_ref& registry) noexcept {
  return type_builder<prefab_description>{registry}
      .serialization<&encode_prefab, &decode_prefab>()
      .commit();
}
}
