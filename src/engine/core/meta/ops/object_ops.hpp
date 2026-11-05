// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/ref.hpp"
#include "engine/core/type_id.hpp"
namespace gneiss::engine::meta {
class object;
/** 注册类型的构造和继承操作；不持有实例，不包含持久化策略。 */
struct object_ops {
  engine::type_id base_type{};
  /** 按注册的 C++ 继承关系调整指针。 */
  void* (*upcast)(void*){};
  ref<void> (*construct)(){};
  object* (*as_object)(void*){};
};
}
