// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/ref.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct yyjson_val;
struct yyjson_mut_val;
struct yyjson_mut_doc;
namespace gneiss::engine::meta {
namespace detail {
struct json_codec_context;
}
class type_registry_state;
/** 原生注册表共享所有者；最后一个引用释放时销毁，借用元数据不得超过该生命周期。 */
using registry_ref = std::shared_ptr<type_registry_state>;
/** 编解码错误码与位置；拥有文本，不依赖资源、文件系统或后端。 */
struct serialization_diagnostic {
  std::size_t byte_offset{};
  std::string path{};
  std::string message{};
  engine::error error{engine::error::success};
};
/** 每种类型可选一组编解码函数，文件格式及版本由 codec
 * 识别。编码对象及输入字节仅在调用期间借用；解码成功返回独立共享所有者。
 * 回调必须可并发操作不同对象；模块需在 Registry 使用期间存活。 */
struct serialization_binding {
  /** 声明引用可承载派生类型；与对象是否被 ref 拥有无关。 */
  bool polymorphic = false;
  engine::type_id base_type{};
  /** 由注册的 C++ 继承关系执行指针调整，不以 void* 猜测基类布局。 */
  void* (*upcast)(void*){};
  /** 显式启用反射 JSON；自定义函数按方向优先。 */
  bool reflected_json = false;
  bool reject_unknown_fields = false;
  /** JSON 节点回调仅借用文档；与字节 codec 互斥，复用当前递归上下文。 */
  result<yyjson_mut_val*, serialization_diagnostic> (*encode_json)(detail::json_codec_context&,
                                                                   yyjson_mut_doc*, const void*){};
  result<ref<void>, serialization_diagnostic> (*decode_json)(detail::json_codec_context&,
                                                             yyjson_val*){};
  std::size_t maximum_json_bytes = std::numeric_limits<std::size_t>::max();
  ref<void> (*construct)(){};
  /** error == success 表示通过；在编码前和解码完成后执行。 */
  serialization_diagnostic (*validate)(const void*){};
  result<std::vector<std::byte>, serialization_diagnostic> (*encode)(const registry_ref&,
                                                                     const void*){};
  result<ref<void>, serialization_diagnostic> (*decode)(const registry_ref&,
                                                        std::span<const std::byte>){};
};
} // namespace gneiss::engine::meta
