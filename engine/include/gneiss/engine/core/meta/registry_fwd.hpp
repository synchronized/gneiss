// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <memory>

namespace gneiss::meta {
class type_registry_state;
/** 原生注册表共享所有者；最后一个引用释放时销毁，借用元数据不得超过该生命周期。 */
using registry_ref = std::shared_ptr<type_registry_state>;
} // namespace gneiss::meta
