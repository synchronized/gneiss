// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/ops/object_ops.hpp"
#include "engine/core/meta/property.hpp"
#include "engine/core/serialization/binding.hpp"
namespace gneiss::engine::meta {
struct type_definition {
  engine::guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const property_definition> properties{};
  object_ops object{};
  serialization::binding serialization{};
  meta::metadata extensions{};
};
}
