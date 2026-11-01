// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
#include <cstdlib>
#include <limits>
#include <yyjson.h>

namespace gneiss::engine::detail {
/** 资源文件外层约束；只包装反射字段，不加载依赖资源。 */
struct reflected_asset_format {
  std::string_view name;
  std::uint64_t version;
  std::size_t maximum_bytes = std::numeric_limits<std::size_t>::max();
  error version_error = error::unsupported;
};
namespace asset_json {
using diagnostic = meta::serialization_diagnostic;
using document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
inline diagnostic invalid(std::string path, std::string message) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
inline result<std::vector<std::byte>, diagnostic> write(yyjson_mut_doc* doc, std::size_t limit) {
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc, 0, &length), &std::free);
  if (!text)
    return diagnostic{.error = error::out_of_memory};
  if (length > limit)
    return invalid("", "资源描述超过文件大小上限");
  const auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
}
template <class T>
result<T, meta::serialization_diagnostic> decode_reflected_asset(std::span<const std::byte> bytes,
                                                                 reflected_asset_format format) {
  using namespace asset_json;
  if (bytes.size() > format.maximum_bytes)
    return invalid("", "资源描述超过文件大小上限");
  yyjson_read_err err{};
  document doc(yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())),
                                bytes.size(), 0, nullptr, &err),
               &yyjson_doc_free);
  if (!doc)
    return diagnostic{err.pos, "", err.msg ? err.msg : "JSON 语法错误",
                      err.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? error::out_of_memory
                                                                      : error::invalid_argument};
  auto* root = yyjson_doc_get_root(doc.get());
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
                                   ? format.version_error
                                   : error::invalid_argument};
  mutable_document fields(yyjson_doc_mut_copy(doc.get(), nullptr), &yyjson_mut_doc_free);
  if (!fields)
    return diagnostic{.error = error::out_of_memory};
  auto* fields_root = yyjson_mut_doc_get_root(fields.get());
  (void)yyjson_mut_obj_remove_key(fields_root, "format");
  (void)yyjson_mut_obj_remove_key(fields_root, "version");
  auto content = write(fields.get(), format.maximum_bytes);
  if (!content)
    return std::move(content).error();
  return meta::deserialize_reflected<T>(*content);
}
template <class T>
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_reflected_asset(const T& object, reflected_asset_format format) {
  using namespace asset_json;
  auto fields = meta::serialize_reflected(object);
  if (!fields)
    return std::move(fields).error();
  document doc(yyjson_read(reinterpret_cast<const char*>(fields->data()), fields->size(), 0),
               &yyjson_doc_free);
  if (!doc)
    return diagnostic{.error = error::out_of_memory};
  mutable_document output(yyjson_doc_mut_copy(doc.get(), nullptr), &yyjson_mut_doc_free);
  if (!output)
    return diagnostic{.error = error::out_of_memory};
  auto* root = yyjson_mut_doc_get_root(output.get());
  if (!yyjson_mut_is_obj(root) || yyjson_mut_obj_get(root, "format") ||
      yyjson_mut_obj_get(root, "version"))
    return invalid("", "反射字段不得占用 format/version 格式头");
  auto* name = yyjson_mut_strncpy(output.get(), format.name.data(), format.name.size());
  if (!name || !yyjson_mut_obj_add_val(output.get(), root, "format", name) ||
      !yyjson_mut_obj_add_uint(output.get(), root, "version", format.version))
    return diagnostic{.error = error::out_of_memory};
  return write(output.get(), format.maximum_bytes);
}
} // namespace gneiss::engine::detail
