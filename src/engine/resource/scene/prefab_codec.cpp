// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/core/error.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include <cstring>

#include <yyjson.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr std::uint64_t prefab_schema_version = 1;
constexpr std::string_view prefab_format = "gneiss.prefab";

void fail(gneiss::engine::scene_diagnostic& diagnostic, gneiss::engine::error result,
          std::string_view path, std::string_view message, std::size_t byte_offset = 0) noexcept {
  diagnostic.error = result;
  diagnostic.byte_offset = byte_offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.error = gneiss::engine::error::out_of_memory;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

[[nodiscard]] gneiss::engine::result<std::string>
convert_author_envelope(std::string_view json, std::string_view identifier, bool to_scene) {
  using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
  document_ptr document(yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG),
                        &yyjson_doc_free);
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr mutable_document(
      document ? yyjson_doc_mut_copy(document.get(), nullptr) : nullptr, &yyjson_mut_doc_free);
  if (!mutable_document) {
    return document ? gneiss::engine::error::out_of_memory : gneiss::engine::error::invalid_state;
  }
  auto* root = yyjson_mut_doc_get_root(mutable_document.get());
  if (!yyjson_mut_is_obj(root)) {
    return gneiss::engine::error::invalid_state;
  }
  (void)yyjson_mut_obj_remove_key(root, "format");
  (void)yyjson_mut_obj_remove_key(root, to_scene ? "prefab_uuid" : "scene_uuid");
  const auto* format = to_scene ? "gneiss.scene" : "gneiss.prefab";
  const auto* identifier_key = to_scene ? "scene_uuid" : "prefab_uuid";
  if (!yyjson_mut_obj_add_str(mutable_document.get(), root, "format", format) ||
      !yyjson_mut_obj_add_strncpy(mutable_document.get(), root, identifier_key, identifier.data(),
                                  identifier.size())) {
    return gneiss::engine::error::out_of_memory;
  }
  std::size_t length = 0U;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!text) {
    return gneiss::engine::error::out_of_memory;
  }
  return std::string(text.get(), length);
}

} // namespace

namespace gneiss::engine {

result<prefab_description, scene_diagnostic>
parse_prefab_description(std::string_view json) noexcept try {
  prefab_description out_prefab;
  scene_diagnostic out_diagnostic;

  yyjson_read_err error{};
  using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
  document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                         YYJSON_READ_NOFLAG, nullptr, &error),
                        &yyjson_doc_free);
  if (!document) {
    fail(out_diagnostic, engine::error::invalid_argument, "", "Prefab JSON 语法错误", error.pos);
    return out_diagnostic;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  yyjson_val* format = yyjson_obj_get(root, "format");
  yyjson_val* version = yyjson_obj_get(root, "version");
  yyjson_val* uuid = yyjson_obj_get(root, "prefab_uuid");
  yyjson_val* objects = yyjson_obj_get(root, "objects");
  if (!yyjson_is_obj(root)) {
    fail(out_diagnostic, engine::error::invalid_argument, "", "Prefab 根值必须是对象");
    return out_diagnostic;
  }
  if (!yyjson_is_str(format) ||
      std::string_view{yyjson_get_str(format), yyjson_get_len(format)} != prefab_format) {
    fail(out_diagnostic, engine::error::invalid_argument, "/format", "不是 Gneiss Prefab 文件");
    return out_diagnostic;
  }
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) != prefab_schema_version) {
    const auto result = yyjson_is_uint(version) && yyjson_get_uint(version) > prefab_schema_version
                            ? engine::error::unsupported
                            : engine::error::invalid_argument;
    fail(out_diagnostic, result, "/version", "不支持的 Prefab Schema 版本");
    return out_diagnostic;
  }
  if (!yyjson_is_str(uuid) ||
      !is_canonical_prefab_uuid({yyjson_get_str(uuid), yyjson_get_len(uuid)})) {
    fail(out_diagnostic, engine::error::invalid_argument, "/prefab_uuid",
         "prefab_uuid 必须是规范的小写 UUID");
    return out_diagnostic;
  }
  if (!yyjson_is_arr(objects)) {
    fail(out_diagnostic, engine::error::invalid_argument, "/objects", "objects 必须是数组");
    return out_diagnostic;
  }

  std::size_t objects_length = 0;
  using json_ptr = std::unique_ptr<char, decltype(&std::free)>;
  json_ptr objects_json(yyjson_val_write(objects, YYJSON_WRITE_NOFLAG, &objects_length),
                        &std::free);
  if (!objects_json) {
    fail(out_diagnostic, engine::error::out_of_memory, "/objects", "无法复制 Prefab 节点");
    return out_diagnostic;
  }
  const std::string_view prefab_uuid{yyjson_get_str(uuid), yyjson_get_len(uuid)};
  std::string scene_json = R"({"format":"gneiss.scene","version":5,"scene_uuid":")";
  scene_json.append(prefab_uuid);
  scene_json += R"(","objects":)";
  scene_json.append(objects_json.get(), objects_length);
  scene_json += "}";

  auto parsed = parse_scene_description(scene_json);
  if (!parsed)
    return std::move(parsed).error();
  auto scene = std::move(*parsed);
  std::size_t root_count = 0;
  for (const auto& object : scene.objects) {
    if (object.prefab) {
      fail(out_diagnostic, engine::error::unsupported, "/objects", "暂不支持嵌套 Prefab 引用");
      return out_diagnostic;
    }
    root_count += object.parent_uuid ? 0U : 1U;
  }
  if (root_count != 1U) {
    fail(out_diagnostic, engine::error::invalid_argument, "/objects",
         "Prefab 必须且只能包含一个根节点");
    return out_diagnostic;
  }

  out_prefab.source_schema_version = 1U;
  out_prefab.uuid = prefab_uuid;
  out_prefab.objects = std::move(scene.objects);
  for (const auto& object : out_prefab.objects) {
    if (object.components.template get<gneiss::engine::mesh_renderer_description>()) {
      out_prefab.dependencies.push_back(
          object.components.template get<gneiss::engine::mesh_renderer_description>()->mesh_uri);
      out_prefab.dependencies.push_back(
          object.components.template get<gneiss::engine::mesh_renderer_description>()
              ->material_uri);
    }
  }
  std::ranges::sort(out_prefab.dependencies);
  const auto unique_end = std::ranges::unique(out_prefab.dependencies).begin();
  out_prefab.dependencies.erase(unique_end, out_prefab.dependencies.end());
  out_prefab.author_json = json;
  return out_prefab;

} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}

result<std::string> serialize_prefab_description(const prefab_description& prefab) noexcept try {

  if (prefab.author_json.empty() || prefab.uuid.empty()) {
    return engine::error::invalid_state;
  }

  scene_description scene;
  scene.source_schema_version = 5U;
  scene.uuid = prefab.uuid;
  scene.objects = prefab.objects;
  auto envelope = convert_author_envelope(prefab.author_json, prefab.uuid, true);
  if (!envelope)
    return envelope.error();
  scene.author_json = std::move(*envelope);
  auto scene_json = serialize_scene_description(scene);
  if (!scene_json)
    return scene_json.error();
  return convert_author_envelope(*scene_json, prefab.uuid, false);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

} // namespace gneiss::engine

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
  std::vector<std::byte> bytes(text->size());
  std::memcpy(bytes.data(), text->data(), text->size());
  auto checked = decode_prefab(bytes);
  if (!checked)
    return std::move(checked).error();
  return bytes;
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}
