// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "asset_file.hpp"
#include "engine/core/serialization/binding.hpp"
#include "engine/core/serialization/detail/json_adapter.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <array>
#include <cstdlib>
#include <yyjson.h>
namespace gneiss::engine::asset_file {
namespace {
using diagnostic = serialization::diagnostic;
inline diagnostic invalid(std::string path, std::string message) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}

inline serialization::diagnostic check_asset_format(yyjson_val* root, format format) {
  if (!yyjson_is_obj(root))
    return invalid("", "资源描述根必须是对象");
  std::size_t formats = 0, versions = 0;
  auto it = yyjson_obj_iter_with(root);
  while (auto* key = yyjson_obj_iter_next(&it)) {
    const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
    formats += name == "format";
    versions += name == "version";
  }
  if (formats > 1 || versions > 1)
    return invalid("", "重复格式头");
  auto* name = yyjson_obj_get(root, "format");
  auto* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_str(name) ||
      std::string_view(yyjson_get_str(name), yyjson_get_len(name)) != format.name)
    return invalid("/format", "资产格式标识错误");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) != format.version)
    return diagnostic{.path = "/version",
                      .message = "不支持的资源格式版本",
                      .error = yyjson_is_uint(version) && yyjson_get_uint(version)
                                   ? error::unsupported
                                   : error::invalid_argument};
  for (auto field : format.rejected_fields)
    if (yyjson_obj_getn(root, field.data(), field.size()))
      return invalid("/" + std::string(field), "该字段已移除，不支持旧格式");
  return {};
}
inline constexpr std::array<std::string_view, 2> asset_headers{"format", "version"};
inline result<yyjson_mut_val*, serialization::diagnostic>
add_asset_header(yyjson_mut_doc* doc, yyjson_mut_val* root, format format) {
  if (!yyjson_mut_is_obj(root) || yyjson_mut_obj_get(root, "format") ||
      yyjson_mut_obj_get(root, "version"))
    return invalid("", "反射字段不得占用 format/version 格式头");
  auto* name = yyjson_mut_strncpy(doc, format.name.data(), format.name.size());
  if (!name || !yyjson_mut_obj_add_val(doc, root, "format", name) ||
      !yyjson_mut_obj_add_uint(doc, root, "version", format.version))
    return diagnostic{.error = error::out_of_memory};
  return root;
}
}

using diagnostic = serialization::diagnostic;
namespace {
constexpr std::string_view retired_fields[]{"prefab_instances"};
}
format format_traits<mesh_description>::get() noexcept { return {"gneiss.mesh", 3}; }
format format_traits<material_description>::get() noexcept { return {"gneiss.material", 6}; }
format format_traits<action_map_description>::get() noexcept {
  return {"gneiss.input-map", 1, maximum_action_map_bytes};
}
format format_traits<scene_description>::get() noexcept {
  return {"gneiss.scene", 5, scene_description::maximum_source_bytes, retired_fields};
}
format format_traits<prefab_description>::get() noexcept {
  return {"gneiss.prefab", 1, prefab_description::maximum_source_bytes, retired_fields};
}
format format_traits<texture_description>::get() noexcept { return {"gneiss.texture", 2}; }
result<std::vector<std::byte>, diagnostic>
serialize_instance(const serialization::context& ctx, format format,
                   const meta::const_instance& view) noexcept try {
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return diagnostic{.error = error::out_of_memory};
  auto root = serialization::json::detail::write_fields(ctx, doc.get(), view);
  if (!root)
    return std::move(root).error();
  root = add_asset_header(doc.get(), *root, format);
  if (!root)
    return std::move(root).error();
  yyjson_mut_doc_set_root(doc.get(), *root);
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &length),
                                                   &std::free);
  if (!text)
    return diagnostic{.error = error::out_of_memory};
  if (length > format.maximum_bytes)
    return invalid("", "资源描述超过文件大小上限");
  auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
} catch (const std::bad_alloc&) {
  return diagnostic{.error = error::out_of_memory};
} catch (...) {
  return diagnostic{.error = error::internal};
}
result<ref<void>, diagnostic> deserialize_object(const serialization::context& ctx, format format,
                                                 type_id id,
                                                 std::span<const std::byte> bytes) noexcept try {
  if (bytes.size() > format.maximum_bytes)
    return invalid("", "资源描述超过文件大小上限");
  yyjson_read_err err{};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       0, nullptr, &err),
      &yyjson_doc_free);
  if (!doc)
    return diagnostic{err.pos, "", err.msg ? err.msg : "JSON 语法错误",
                      err.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? error::out_of_memory
                                                                      : error::invalid_argument};
  auto* root = yyjson_doc_get_root(doc.get());
  auto status = check_asset_format(root, format);
  if (status.error.failed())
    return status;
  auto decoded = serialization::json::detail::read_fields_object(ctx, root, id, asset_headers);
  if (!decoded)
    return std::move(decoded).error();
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  auto binding = serialization::find_serialization(*registry, id);
  if (!binding)
    return diagnostic{.error = binding.error()};
  if (binding->validate) {
    auto validation = binding->validate(decoded->get());
    if (validation.error.failed())
      return validation;
  }
  return decoded;
} catch (const std::bad_alloc&) {
  return diagnostic{.error = error::out_of_memory};
} catch (...) {
  return diagnostic{.error = error::internal};
}
}
