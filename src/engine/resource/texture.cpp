// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"

namespace gneiss::engine {
texture::texture(rid value) noexcept : asset(runtime_type_of<texture>()), rid_(value) {}
texture::~texture() = default;
rid resource_handle(const ref<const texture>& value) noexcept {
  return value ? detail::texture_resource_access::handle(*value) : null_rid;
}
} // namespace gneiss::engine
