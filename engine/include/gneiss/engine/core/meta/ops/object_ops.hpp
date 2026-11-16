// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/types/ref.hpp>
#include <gneiss/engine/core/types/type_id.hpp>

namespace gneiss::meta {
class object;
/** 注册类型的构造和继承操作；不持有实例，不包含持久化策略。 */
struct object_ops {
  gneiss::type_id base_type{};
  /** 按注册的 C++ 继承关系调整指针。 */
  void* (*upcast)(void*){};
  ref<void> (*construct)(){};
  object* (*as_object)(void*){};
};
}
