// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_description.h"

#include "asset/virtual_file_system.h"

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

void fail(gneiss::scene_internal::scene_diagnostic& diagnostic, gneiss_result result,
          std::string_view path, std::string_view message, std::size_t byte_offset = 0) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = byte_offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = GNEISS_ERROR_OUT_OF_MEMORY;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

[[nodiscard]] gneiss_result convert_author_envelope(std::string_view json,
                                                    std::string_view identifier, bool to_scene,
                                                    std::string& output) {
  using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
  document_ptr document(yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG),
                        &yyjson_doc_free);
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr mutable_document(
      document ? yyjson_doc_mut_copy(document.get(), nullptr) : nullptr, &yyjson_mut_doc_free);
  if (!mutable_document) {
    return document ? GNEISS_ERROR_OUT_OF_MEMORY : GNEISS_ERROR_INVALID_STATE;
  }
  auto* root = yyjson_mut_doc_get_root(mutable_document.get());
  if (!yyjson_mut_is_obj(root)) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  (void)yyjson_mut_obj_remove_key(root, "format");
  (void)yyjson_mut_obj_remove_key(root, to_scene ? "prefab_uuid" : "scene_uuid");
  (void)yyjson_mut_obj_remove_key(root, "prefab_instances");
  const auto* format = to_scene ? "gneiss.scene" : "gneiss.prefab";
  const auto* identifier_key = to_scene ? "scene_uuid" : "prefab_uuid";
  if (!yyjson_mut_obj_add_str(mutable_document.get(), root, "format", format) ||
      !yyjson_mut_obj_add_strncpy(mutable_document.get(), root, identifier_key, identifier.data(),
                                  identifier.size()) ||
      (to_scene &&
       yyjson_mut_obj_add_arr(mutable_document.get(), root, "prefab_instances") == nullptr)) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  std::size_t length = 0U;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!text) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  output.assign(text.get(), length);
  return GNEISS_SUCCESS;
}

} // namespace

namespace gneiss::scene_internal {

gneiss_result parse_prefab_description(std::string_view json, prefab_description& out_prefab,
                                       scene_diagnostic& out_diagnostic) noexcept {
  out_prefab = {};
  out_diagnostic = {};
  try {
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (!document) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "Prefab JSON 语法错误", error.pos);
      return out_diagnostic.result;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    yyjson_val* format = yyjson_obj_get(root, "format");
    yyjson_val* version = yyjson_obj_get(root, "version");
    yyjson_val* uuid = yyjson_obj_get(root, "prefab_uuid");
    yyjson_val* objects = yyjson_obj_get(root, "objects");
    if (!yyjson_is_obj(root)) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "Prefab 根值必须是对象");
      return out_diagnostic.result;
    }
    if (!yyjson_is_str(format) ||
        std::string_view{yyjson_get_str(format), yyjson_get_len(format)} != prefab_format) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/format", "不是 Gneiss Prefab 文件");
      return out_diagnostic.result;
    }
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != prefab_schema_version) {
      const auto result =
          yyjson_is_uint(version) && yyjson_get_uint(version) > prefab_schema_version
              ? GNEISS_ERROR_UNSUPPORTED
              : GNEISS_ERROR_INVALID_ARGUMENT;
      fail(out_diagnostic, result, "/version", "不支持的 Prefab Schema 版本");
      return out_diagnostic.result;
    }
    if (!yyjson_is_str(uuid) ||
        !is_canonical_prefab_uuid({yyjson_get_str(uuid), yyjson_get_len(uuid)})) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/prefab_uuid",
           "prefab_uuid 必须是规范的小写 UUID");
      return out_diagnostic.result;
    }
    if (!yyjson_is_arr(objects)) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/objects", "objects 必须是数组");
      return out_diagnostic.result;
    }

    std::size_t objects_length = 0;
    using json_ptr = std::unique_ptr<char, decltype(&std::free)>;
    json_ptr objects_json(yyjson_val_write(objects, YYJSON_WRITE_NOFLAG, &objects_length),
                          &std::free);
    if (!objects_json) {
      fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "/objects", "无法复制 Prefab 节点");
      return out_diagnostic.result;
    }
    const std::string_view prefab_uuid{yyjson_get_str(uuid), yyjson_get_len(uuid)};
    std::string scene_json = R"({"format":"gneiss.scene","version":4,"scene_uuid":")";
    scene_json.append(prefab_uuid);
    scene_json += R"(","objects":)";
    scene_json.append(objects_json.get(), objects_length);
    scene_json += ",\"prefab_instances\":[]}";

    scene_description scene;
    auto result = parse_scene_description(scene_json, scene, out_diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::size_t root_count = 0;
    for (const auto& object : scene.objects) {
      root_count += object.parent_uuid ? 0U : 1U;
    }
    if (root_count != 1U) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/objects",
           "Prefab 必须且只能包含一个根节点");
      return out_diagnostic.result;
    }

    out_prefab.source_schema_version = 1U;
    out_prefab.uuid = prefab_uuid;
    out_prefab.objects = std::move(scene.objects);
    for (const auto& object : out_prefab.objects) {
      if (object.mesh_renderer) {
        out_prefab.dependencies.push_back(object.mesh_renderer->mesh_uri);
        out_prefab.dependencies.push_back(object.mesh_renderer->material_uri);
      }
    }
    std::ranges::sort(out_prefab.dependencies);
    const auto unique_end = std::ranges::unique(out_prefab.dependencies).begin();
    out_prefab.dependencies.erase(unique_end, out_prefab.dependencies.end());
    out_prefab.author_json = json;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    out_prefab = {};
    fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "内存不足");
    return out_diagnostic.result;
  } catch (...) {
    out_prefab = {};
    fail(out_diagnostic, GNEISS_ERROR_INTERNAL, "", "Prefab 解析内部错误");
    return out_diagnostic.result;
  }
}

gneiss_result load_prefab_description(const asset_internal::virtual_file_system& file_system,
                                      std::string_view uri, prefab_description& out_prefab,
                                      scene_diagnostic& out_diagnostic) noexcept {
  std::vector<std::byte> bytes;
  const auto result = file_system.read(uri, bytes);
  if (result != GNEISS_SUCCESS) {
    out_prefab = {};
    out_diagnostic = {};
    fail(out_diagnostic, result, "", "无法通过 VFS 读取 Prefab");
    return result;
  }
  return parse_prefab_description(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), out_prefab,
      out_diagnostic);
}

gneiss_result serialize_prefab_description(const prefab_description& prefab,
                                           std::string& out_json) noexcept {
  if (prefab.author_json.empty() || prefab.uuid.empty()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    scene_description scene;
    scene.source_schema_version = 4U;
    scene.uuid = prefab.uuid;
    scene.objects = prefab.objects;
    auto result = convert_author_envelope(prefab.author_json, prefab.uuid, true, scene.author_json);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::string scene_json;
    result = serialize_scene_description(scene, scene_json);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    return convert_author_envelope(scene_json, prefab.uuid, false, out_json);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::scene_internal
