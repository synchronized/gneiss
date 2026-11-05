// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
struct yyjson_val;
struct yyjson_mut_val;
struct yyjson_mut_doc;
namespace gneiss::engine::meta::detail {
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_instance(yyjson_mut_doc*, const registry_ref&, const instance&) noexcept;
GNEISS_API serialization_diagnostic read_instance(yyjson_val*, const registry_ref&,
                                                  const mutable_instance&) noexcept;
/** 使用视图自身的注册表。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_instance(yyjson_mut_doc*, const instance&) noexcept;
GNEISS_API serialization_diagnostic read_instance(yyjson_val*, const mutable_instance&) noexcept;
/** 内部 JSON 适配：类型标签保留 value 的精确类别，标量规则与反射属性共用。 */
GNEISS_API result<value, serialization_diagnostic> read_tagged_value(yyjson_val*) noexcept;
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_tagged_value(yyjson_mut_doc*, const value&) noexcept;
/** 只遍历本类型字段；保留嵌套 codec、校验、注册表和递归深度。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_reflected(json_codec_context&, yyjson_mut_doc*, type_id, const void*);
GNEISS_API result<ref<void>, serialization_diagnostic>
read_reflected(json_codec_context&, yyjson_val*, type_id,
               std::span<const std::string_view> ignored = {});
/** 独立文件入口：借用视图绑定的注册表，建立一次递归上下文。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic> write_reflected(yyjson_mut_doc*,
                                                                             const instance&);
GNEISS_API serialization_diagnostic read_reflected(yyjson_val*, const mutable_instance&,
                                                   std::span<const std::string_view> ignored = {});
template <class T>
result<T, serialization_diagnostic> read_reflected(json_codec_context& ctx, yyjson_val* input,
                                                   std::span<const std::string_view> ignored = {}) {
  auto decoded = read_reflected(ctx, input, runtime_type_of<T>(), ignored);
  if (!decoded)
    return std::move(decoded).error();
  return std::move(*static_cast<T*>(decoded->get()));
}
}
