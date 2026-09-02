// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_description.h"

#include <yyjson.h>

#include <cstdlib>
#include <memory>
#include <new>

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

[[nodiscard]] bool is_canonical_uuid(std::string_view value) noexcept {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

} // namespace

namespace gneiss::scene_internal {

bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept {
  return is_canonical_uuid(address.instance_uuid) && is_canonical_uuid(address.source_node_uuid);
}

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
    if (!yyjson_is_str(uuid) || !is_canonical_uuid({yyjson_get_str(uuid), yyjson_get_len(uuid)})) {
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
    std::string scene_json = "{\"format\":\"gneiss.scene\",\"version\":2,\"scene_uuid\":\"";
    scene_json.append(prefab_uuid);
    scene_json += "\",\"objects\":";
    scene_json.append(objects_json.get(), objects_length);
    scene_json += '}';

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

} // namespace gneiss::scene_internal
