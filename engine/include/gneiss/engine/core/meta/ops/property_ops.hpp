// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/base/error.hpp>

namespace gneiss::meta {
struct type_ops;
/** 属性访问表；注册时复制回调，state 借用且须覆盖使用期。
 * read_construct 成功时在未初始化目标中构造一个 access 对应的值；失败或抛异常时不得留下活对象。
 * write 只借用输入，失败必须保持宿主不变。borrow 返回稳定成员地址，不得返回临时值地址。 */
struct property_ops {
  const type_ops* access{};
  void* state{};
  const void* (*const_borrow)(void*, const void*){};
  void* (*borrow)(void*, void*){};
  error (*read_construct)(void*, const void*, void*){};
  error (*write)(void*, void*, const void*){};
};
}
