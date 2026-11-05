// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/ref.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace gneiss::engine::serialization {
class context;
class reader;
class writer;
}
namespace gneiss::engine::meta {
class object;
/** 编解码错误码与位置；拥有文本，不依赖资源、文件系统或后端。 */
struct serialization_diagnostic {
  std::size_t byte_offset{};
  std::string path{};
  std::string message{};
  engine::error error{engine::error::success};
};
/** 类型构造、继承及校验与具体格式操作分开保存。编码对象及输入字节仅在调用期间借用；解码成功返回独立共享所有者。
 * 回调必须可并发操作不同对象；模块需在 Registry 使用期间存活。 */
struct serialization_binding {
  /** 启用格式无关字段遍历；自定义 codec 按方向优先。 */
  bool reflected = false;
  bool reject_unknown_fields = false;
  /** 声明引用可承载派生类型；与对象是否被 ref 拥有无关。 */
  bool polymorphic = false;
  engine::type_id base_type{};
  /** 由注册的 C++ 继承关系执行指针调整，不以 void* 猜测基类布局。 */
  void* (*upcast)(void*){};
  /** 借用 reader/writer 节点；回调只处理对象内容，不读写文件。 */
  result<void*, serialization_diagnostic> (*encode)(const serialization::context&,
                                                    serialization::writer&, const void*){};
  result<ref<void>, serialization_diagnostic> (*decode)(const serialization::context&,
                                                        serialization::reader&, const void*){};
  ref<void> (*construct)(){};
  /** 编译期确认派生关系，按 C++ 布局转换完整对象地址。 */
  object* (*as_object)(void*){};
  /** error == success 表示通过；在编码前和解码完成后执行。 */
  serialization_diagnostic (*validate)(const void*){};
};
} // namespace gneiss::engine::meta
