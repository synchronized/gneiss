// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/json.hpp"
struct yyjson_val;
struct yyjson_mut_val;
struct yyjson_mut_doc;
namespace gneiss::serialization::json::detail {
using meta::const_instance;
using meta::instance;
using meta::registry_ref;
/** 只遍历根字段，跳过根 codec，保留嵌套 codec 及同一 context 的限制。 */
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic>
write_fields_object(const write_context&, yyjson_mut_doc*, type_id, const void*);
GNEISS_API result<ref<void>, serialization::diagnostic>
read_fields_object(const read_context&, yyjson_val*, type_id, std::span<const std::string_view>);
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic>
write_value(const write_context&, yyjson_mut_doc*, const value&);
GNEISS_API result<value, serialization::diagnostic> read_value(const read_context&, yyjson_val*);
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic>
write(const write_context&, yyjson_mut_doc*, const const_instance&);
GNEISS_API serialization::diagnostic read(const read_context&, yyjson_val*, const instance&);
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic>
write_fields(const write_context&, yyjson_mut_doc*, const const_instance&);
GNEISS_API serialization::diagnostic read_fields(const read_context&, yyjson_val*, const instance&,
                                                 std::span<const std::string_view> = {});
/** 内部 JSON 适配边界；普通业务使用 serialize/deserialize。节点只在所属文档存活期间有效。 */

/** 通用带标签值；标量无需冻结表，对象分支要求默认表已注册并冻结。 */
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic> write_value(yyjson_mut_doc*,
                                                                          const value&);
GNEISS_API result<value, serialization::diagnostic> read_value(yyjson_val*);

/** 完整视图编解码，使用视图自身的注册表。输出节点由 doc 拥有；读取失败不保证恢复目标。 */
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic> write(yyjson_mut_doc* doc,
                                                                    const const_instance&);
GNEISS_API serialization::diagnostic read(yyjson_val*, const instance&);
/** 独立格式包装的字段入口，跳过根 codec，保留校验；ignored 仅作用于根字段。 */
GNEISS_API result<yyjson_mut_val*, serialization::diagnostic> write_fields(yyjson_mut_doc*,
                                                                           const const_instance&);
GNEISS_API serialization::diagnostic read_fields(yyjson_val*, const instance&,
                                                 std::span<const std::string_view> ignored = {});
}
