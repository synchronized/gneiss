// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/ref.hpp"
namespace gneiss::engine {
/** 资源使用与普通对象相同的控制块；销毁策略由资源工厂提供。 */
template <class T> using asset_ref = ref<T>;
}
