// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture/texture_resource.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"

namespace gneiss::engine {
texture_resource::texture_resource(rid value) noexcept : rid_(value) {}
texture_resource::~texture_resource() = default;
rid resource_handle(const texture& value) noexcept {
  return value ? detail::texture_resource_access::handle(*value) : null_rid;
}
} // namespace gneiss::engine
