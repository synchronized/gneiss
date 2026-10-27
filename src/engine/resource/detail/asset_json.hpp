// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/asset_diagnostic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include <yyjson.h>

namespace gneiss::engine::detail {
[[nodiscard]] inline std::string_view json_string(yyjson_val* value) {
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] inline bool has_only_fields(yyjson_val* object,
                                          std::span<const std::string_view> fields,
                                          asset_diagnostic& diagnostic) {
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_obj_foreach(object, index, maximum, key, value) {
    (void)value;
    const auto name = json_string(key);
    if (std::ranges::find(fields, name) == fields.end()) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/" + std::string(name),
           "未知字段");
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool validate_header(yyjson_val* root, std::string_view expected_format,
                                          std::span<const std::string_view> fields,
                                          std::uint64_t maximum_version, std::uint64_t& out_version,
                                          asset_diagnostic& diagnostic) {
  if (!yyjson_is_obj(root) || !has_only_fields(root, fields, diagnostic)) {
    if (diagnostic.result == gneiss::engine::result::success) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "", "资产根必须是对象");
    }
    return false;
  }
  yyjson_val* format = yyjson_obj_get(root, "format");
  if (!yyjson_is_str(format) || json_string(format) != expected_format) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/format", "资产格式标识错误");
    return false;
  }
  yyjson_val* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) == 0U ||
      yyjson_get_uint(version) > maximum_version) {
    fail(diagnostic,
         yyjson_is_uint(version) && yyjson_get_uint(version) > 1U
             ? gneiss::engine::result::unsupported
             : gneiss::engine::result::invalid_argument,
         "/version", "不支持的资产版本");
    return false;
  }
  out_version = yyjson_get_uint(version);
  return true;
}

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

[[nodiscard]] inline document_ptr parse_document(const std::vector<std::byte>& bytes,
                                                 asset_diagnostic& diagnostic) {
  if (bytes.empty()) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "", "资产文档为空");
    return {nullptr, &yyjson_doc_free};
  }
  yyjson_read_err error{};
  document_ptr document(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       YYJSON_READ_NOFLAG, nullptr, &error),
      &yyjson_doc_free);
  if (!document) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "",
         error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
  }
  return {document.release(), &yyjson_doc_free};
}

[[nodiscard]] inline bool read_float(yyjson_val* value, float& output) {
  const double number = yyjson_get_num(value);
  if (!yyjson_is_num(value) || !std::isfinite(number) ||
      std::abs(number) > std::numeric_limits<float>::max()) {
    return false;
  }
  output = static_cast<float>(number);
  return true;
}

}
