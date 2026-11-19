// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/detail/type_definition.hpp>

// 类型查询入口统一提供内置能力；能力实现仅依赖类型定义，避免反向包含本汇总头。
#include <gneiss/engine/core/meta/capabilities/container.hpp>
#include <gneiss/engine/core/meta/capabilities/enumeration.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/capabilities/representation.hpp>
#include <gneiss/engine/core/meta/capabilities/sequence.hpp>
#include <gneiss/engine/core/meta/capabilities/variant.hpp>
