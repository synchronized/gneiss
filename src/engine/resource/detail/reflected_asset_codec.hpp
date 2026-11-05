// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/json_serialization.hpp"
#include "engine/core/meta/registration.hpp"
#include <array>
#include <cstdlib>
#include <limits>
#include <yyjson.h>

namespace gneiss::engine::detail {
/** 资源文件外层约束；只包装反射字段，不加载依赖资源。 */
struct reflected_asset_format {
  std::string_view name;
  std::uint64_t version;
  std::size_t maximum_bytes = std::numeric_limits<std::size_t>::max();
  std::span<const std::string_view> rejected_fields{};
};
namespace asset_json {
using diagnostic = meta::serialization_diagnostic;
inline diagnostic invalid(std::string path, std::string message) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
}

inline meta::serialization_diagnostic check_asset_format(yyjson_val* root,
                                                         reflected_asset_format format) {
  using namespace asset_json;
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
template <class T>
result<T, meta::serialization_diagnostic> decode_reflected_asset(meta::json_codec_context& ctx,
                                                                 yyjson_val* root,
                                                                 reflected_asset_format format) {
  auto status = check_asset_format(root, format);
  if (status.error.failed())
    return status;
  return ctx.read_fields<T>(root, asset_headers);
}

inline result<yyjson_mut_val*, meta::serialization_diagnostic>
add_asset_header(yyjson_mut_doc* doc, yyjson_mut_val* root, reflected_asset_format format) {
  using namespace asset_json;
  if (!yyjson_mut_is_obj(root) || yyjson_mut_obj_get(root, "format") ||
      yyjson_mut_obj_get(root, "version"))
    return invalid("", "反射字段不得占用 format/version 格式头");
  auto* name = yyjson_mut_strncpy(doc, format.name.data(), format.name.size());
  if (!name || !yyjson_mut_obj_add_val(doc, root, "format", name) ||
      !yyjson_mut_obj_add_uint(doc, root, "version", format.version))
    return diagnostic{.error = error::out_of_memory};
  return root;
}
template <class T>
result<yyjson_mut_val*, meta::serialization_diagnostic>
encode_reflected_asset(meta::json_codec_context& ctx, yyjson_mut_doc* doc, const T& object,
                       reflected_asset_format format) {
  auto fields = ctx.write_fields(doc, object);
  if (!fields)
    return std::move(fields).error();
  return add_asset_header(doc, *fields, format);
}
/** 纹理等独立文件适配入口；只解析/输出一次，字段使用同一 DOM。 */
template <class T>
result<T, meta::serialization_diagnostic> decode_reflected_asset(std::span<const std::byte> bytes,
                                                                 reflected_asset_format format) {
  using namespace asset_json;
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
  T object{};
  auto view = meta::mutable_instance::from(object);
  if (!view)
    return diagnostic{.error = view.error()};
  status = meta::json::read_fields(root, *view, asset_headers);
  if (status.error.failed())
    return status;
  return object;
}
template <class T>
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_reflected_asset(const T& object, reflected_asset_format format) {
  using namespace asset_json;
  auto view = meta::instance::from(object);
  if (!view)
    return diagnostic{.error = view.error()};
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return diagnostic{.error = error::out_of_memory};
  auto root = meta::json::write_fields(doc.get(), *view);
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
}
/** 复用字段注册和校验，只追加文件格式包装并提交类型。 */
template <class T, const reflected_asset_format& Format>
error register_reflected_asset(meta::type_builder<T>& builder) noexcept {
  constexpr auto encode = [](meta::json_codec_context& ctx, yyjson_mut_doc* doc, const T& object) {
    return encode_reflected_asset(ctx, doc, object, Format);
  };
  constexpr auto decode = [](meta::json_codec_context& ctx, yyjson_val* input) {
    return decode_reflected_asset<T>(ctx, input, Format);
  };
  return builder.template json_serialization<encode, decode>(Format.maximum_bytes).commit();
}
} // namespace gneiss::engine::detail
