// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_REFLECTION_INL_
#define GNEISS_DETAIL_REFLECTION_INL_

#include "engine/values/reflection.hpp"

#include <limits>
#include <new>
#include <stdexcept>

namespace gneiss {

namespace detail {
inline field_info reflection_field(const gneiss_field_info& value) noexcept {
  return {
      .id = field_id{value.id},
      .value_type_id = from_native(value.value_type_id),
      .flags = static_cast<field_flags>(value.flags),
      .name = value.name == nullptr ? std::string_view{}
                                    : std::string_view{value.name, value.name_length},
      .kind = static_cast<property_kind>(value.property_kind),
      .capabilities = static_cast<property_capabilities>(value.property_capabilities),
  };
}
inline result reflection_type(const gneiss_type_info& value, type_info& output) noexcept {
  try {
    type_info candidate;
    candidate.id = from_native(value.id);
    candidate.schema_version = value.schema_version;
    candidate.name = value.name == nullptr ? std::string_view{}
                                           : std::string_view{value.name, value.name_length};
    candidate.fields.reserve(value.field_count);
    for (std::uint32_t i = 0; i < value.field_count; ++i) {
      candidate.fields.push_back(reflection_field(value.fields[i]));
    }
    output = std::move(candidate);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}

} // namespace detail

} // namespace gneiss

#endif
