// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
namespace gneiss::engine::meta {
namespace detail {
struct json_codec_state;
}
/** 仅供 JSON codec 借用；保留当前注册表、递归深度和引用环检测状态，不能复制或保存。 */
class json_codec_context {
  friend struct detail::json_codec_state;
  detail::json_codec_state* state_;
  explicit json_codec_context(detail::json_codec_state& state) noexcept : state_(&state) {}
  GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
  write_fields_object(yyjson_mut_doc*, type_id, const void*);
  GNEISS_API result<ref<void>, serialization_diagnostic>
  read_fields_object(yyjson_val*, type_id, std::span<const std::string_view>);

public:
  json_codec_context(const json_codec_context&) = delete;
  json_codec_context& operator=(const json_codec_context&) = delete;
  /** 只遍历本类型字段；跳过本类型 codec，嵌套 codec 仍生效，外层入口负责对象校验。 */
  template <class T>
  result<yyjson_mut_val*, serialization_diagnostic> write_fields(yyjson_mut_doc* doc,
                                                                 const T& object) {
    return write_fields_object(doc, runtime_type_of<T>(), std::addressof(object));
  }
  template <class T>
  result<T, serialization_diagnostic> read_fields(yyjson_val* input,
                                                  std::span<const std::string_view> ignored = {}) {
    auto decoded = read_fields_object(input, runtime_type_of<T>(), ignored);
    if (!decoded)
      return std::move(decoded).error();
    return std::move(*static_cast<T*>(decoded->get()));
  }
};
/** 内部 JSON 适配边界；普通业务使用 serialize/deserialize。节点只在所属文档存活期间有效。 */
namespace json {
/** 完整视图编解码，使用视图自身的注册表。输出节点由 doc 拥有；读取失败不保证恢复目标。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic> write(yyjson_mut_doc* doc,
                                                                   const instance&) noexcept;
GNEISS_API serialization_diagnostic read(yyjson_val*, const mutable_instance&) noexcept;
/** 独立格式包装的字段入口，跳过根 codec，保留校验；ignored 仅作用于根字段。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic> write_fields(yyjson_mut_doc*,
                                                                          const instance&) noexcept;
GNEISS_API serialization_diagnostic read_fields(
    yyjson_val*, const mutable_instance&, std::span<const std::string_view> ignored = {}) noexcept;
}
}
