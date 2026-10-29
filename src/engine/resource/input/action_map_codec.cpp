// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "action_map_codec.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <yyjson.h>
namespace gneiss::engine {
namespace {
using diagnostic = meta::serialization_diagnostic;
using document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
diagnostic invalid(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
diagnostic validate_binding(const action_binding& binding) {
  if (binding.physical_key == 0 || binding.physical_key >= 256 || !std::isfinite(binding.scale) ||
      binding.scale == 0 || binding.scale < -1 || binding.scale > 1)
    return invalid("", "按键或 scale 超出有效范围");
  return {};
}
result<std::vector<std::byte>, diagnostic> write_json(yyjson_mut_doc* doc) {
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc, 0, &length), &std::free);
  if (!text)
    return diagnostic{.error = error::out_of_memory};
  const auto bytes = std::as_bytes(std::span{text.get(), length});
  if (length > maximum_action_map_bytes)
    return invalid("", "输入映射超过 1 MiB 上限");
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
result<action_map, diagnostic> decode(std::span<const std::byte> bytes) {
  if (bytes.size() > maximum_action_map_bytes)
    return invalid("", "输入映射超过 1 MiB 上限");
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
    return invalid("", "输入映射根必须是对象");
  auto* format = yyjson_obj_get(root, "format");
  auto* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_str(format) ||
      std::string_view(yyjson_get_str(format), yyjson_get_len(format)) != "gneiss.input-map")
    return invalid("/format", "资产格式标识错误");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 1)
    return invalid("/version", "输入映射仅支持版本 1");
  std::size_t formats = 0, versions = 0;
  auto it = yyjson_obj_iter_with(root);
  while (auto* key = yyjson_obj_iter_next(&it)) {
    const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
    formats += name == "format";
    versions += name == "version";
  }
  if (formats != 1 || versions != 1)
    return invalid("", "重复格式头");
  mutable_document fields(yyjson_doc_mut_copy(doc.get(), nullptr), &yyjson_mut_doc_free);
  if (!fields)
    return diagnostic{.error = error::out_of_memory};
  auto* fields_root = yyjson_mut_doc_get_root(fields.get());
  (void)yyjson_mut_obj_remove_key(fields_root, "format");
  (void)yyjson_mut_obj_remove_key(fields_root, "version");
  auto content = write_json(fields.get());
  if (!content)
    return std::move(content).error();
  return meta::deserialize_reflected<action_map>(*content);
}
result<std::vector<std::byte>, diagnostic> encode(const action_map& object) {
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
  if (!yyjson_mut_obj_add_str(output.get(), root, "format", "gneiss.input-map") ||
      !yyjson_mut_obj_add_uint(output.get(), root, "version", 1))
    return diagnostic{.error = error::out_of_memory};
  return write_json(output.get());
}
}
error register_action_map_reflection() noexcept {
  constexpr auto required = meta::property_flags::required;
  auto status = meta::type_builder<action_binding>{}
                    .property<&action_binding::physical_key>(meta::property_id{1}, "key", required)
                    .property<&action_binding::scale>(meta::property_id{2}, "scale", required)
                    .serialization(true)
                    .validate<&validate_binding>()
                    .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<action_definition>{}
               .property<&action_definition::name>(meta::property_id{1}, "name", required)
               .property<&action_definition::bindings>(meta::property_id{2}, "bindings", required)
               .serialization(true)
               .commit();
  if (status.failed())
    return status;
  return meta::type_builder<action_map>{}
      .schema_version(1)
      .property<&action_map::actions>(meta::property_id{1}, "actions", required)
      .serialization(true)
      .serialization<&encode, &decode>()
      .validate<&validate_action_map>()
      .commit();
}
result<action_map, diagnostic> parse_action_map(std::string_view json) noexcept {
  return decode_action_map(std::as_bytes(std::span{json.data(), json.size()}));
}
result<action_map, diagnostic> decode_action_map(std::span<const std::byte> bytes) noexcept {
  return meta::deserialize<action_map>(bytes);
}
result<std::vector<std::byte>, diagnostic> encode_action_map(const action_map& object) noexcept {
  return meta::serialize(object);
}
}
