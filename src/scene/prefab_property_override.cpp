// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_property_override.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

using namespace gneiss::scene_internal;

[[nodiscard]] bool is_zero_type_id(gneiss_type_id value) noexcept {
  return std::ranges::all_of(value.bytes, [](std::uint8_t byte) { return byte == 0U; });
}

[[nodiscard]] bool is_canonical_uuid_impl(std::string_view value) noexcept {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
  std::size_t index = 0U;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first == 0U) {
      return false;
    }
    if (first < 0x80U) {
      ++index;
      continue;
    }
    std::size_t continuation_count = 0U;
    std::uint32_t code_point = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1U;
      code_point = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2U;
      code_point = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3U;
      code_point = first & 0x07U;
    } else {
      return false;
    }
    if (index + continuation_count >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if ((continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1U;
  }
  return true;
}

[[nodiscard]] bool valid_value(const prefab_property_value& value) noexcept {
  return std::visit(
      [](const auto& payload) noexcept {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, std::monostate>) {
          return false;
        } else if constexpr (std::is_same_v<value_type, float> ||
                             std::is_same_v<value_type, double>) {
          return std::isfinite(payload);
        } else if constexpr (std::is_same_v<value_type, std::string>) {
          return valid_utf8(payload);
        } else if constexpr (std::is_same_v<value_type, std::array<std::uint8_t, 16>>) {
          return std::ranges::any_of(payload, [](std::uint8_t byte) { return byte != 0U; });
        } else if constexpr (std::is_same_v<value_type, std::array<float, 3>> ||
                             std::is_same_v<value_type, std::array<float, 4>>) {
          return std::ranges::all_of(payload, [](float item) { return std::isfinite(item); });
        } else {
          return true;
        }
      },
      value.payload);
}

void normalize_zero(prefab_property_value& value) noexcept {
  std::visit(
      [](auto& payload) noexcept {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
          if (payload == 0) {
            payload = 0;
          }
        } else if constexpr (std::is_same_v<value_type, std::array<float, 3>> ||
                             std::is_same_v<value_type, std::array<float, 4>>) {
          for (auto& item : payload) {
            if (item == 0.0F) {
              item = 0.0F;
            }
          }
        }
      },
      value.payload);
}

} // namespace

namespace gneiss::scene_internal {

bool is_canonical_prefab_uuid(std::string_view value) noexcept {
  return is_canonical_uuid_impl(value);
}

bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept {
  return is_canonical_prefab_uuid(address.instance_uuid) &&
         is_canonical_prefab_uuid(address.source_node_uuid);
}

bool prefab_property_override_key_less(const prefab_property_override_key& left,
                                       const prefab_property_override_key& right) noexcept {
  if (left.node.instance_uuid != right.node.instance_uuid) {
    return left.node.instance_uuid < right.node.instance_uuid;
  }
  if (left.node.source_node_uuid != right.node.source_node_uuid) {
    return left.node.source_node_uuid < right.node.source_node_uuid;
  }
  if (!std::ranges::equal(left.type_id.bytes, right.type_id.bytes)) {
    return std::lexicographical_compare(
        std::begin(left.type_id.bytes), std::end(left.type_id.bytes),
        std::begin(right.type_id.bytes), std::end(right.type_id.bytes));
  }
  return left.field_id < right.field_id;
}

bool prefab_property_override_key::operator==(
    const prefab_property_override_key& other) const noexcept {
  return node == other.node && field_id == other.field_id &&
         std::ranges::equal(type_id.bytes, other.type_id.bytes);
}

gneiss_property_kind prefab_property_value_kind(const prefab_property_value& value) noexcept {
  return std::visit(
      [](const auto& payload) noexcept -> gneiss_property_kind {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, bool>) {
          return GNEISS_PROPERTY_KIND_BOOL;
        } else if constexpr (std::is_same_v<value_type, std::int64_t>) {
          return GNEISS_PROPERTY_KIND_INT64;
        } else if constexpr (std::is_same_v<value_type, std::uint64_t>) {
          return GNEISS_PROPERTY_KIND_UINT64;
        } else if constexpr (std::is_same_v<value_type, float>) {
          return GNEISS_PROPERTY_KIND_FLOAT32;
        } else if constexpr (std::is_same_v<value_type, double>) {
          return GNEISS_PROPERTY_KIND_FLOAT64;
        } else if constexpr (std::is_same_v<value_type, std::string>) {
          return GNEISS_PROPERTY_KIND_STRING;
        } else if constexpr (std::is_same_v<value_type, std::array<std::uint8_t, 16>>) {
          return GNEISS_PROPERTY_KIND_TYPE_ID;
        } else if constexpr (std::is_same_v<value_type, std::array<float, 3>>) {
          return GNEISS_PROPERTY_KIND_VEC3;
        } else if constexpr (std::is_same_v<value_type, std::array<float, 4>>) {
          return GNEISS_PROPERTY_KIND_QUATERNION;
        } else {
          return GNEISS_PROPERTY_KIND_INVALID;
        }
      },
      value.payload);
}

gneiss_result validate_prefab_property_override(gneiss_type_registry registry,
                                                const prefab_property_override& value) noexcept {
  if (!is_valid_prefab_author_address(value.key.node) || is_zero_type_id(value.key.type_id) ||
      value.key.field_id == GNEISS_NULL_FIELD_ID || !valid_value(value.value)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  gneiss_field_info field = GNEISS_FIELD_INFO_INIT;
  const auto found =
      gneiss_type_registry_find_field(registry, value.key.type_id, value.key.field_id, &field);
  if (found != GNEISS_SUCCESS) {
    return found;
  }
  if (field.property_kind != prefab_property_value_kind(value.value) ||
      (field.property_capabilities & GNEISS_PROPERTY_CAPABILITY_WRITABLE) == 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return GNEISS_SUCCESS;
}

gneiss_result set_prefab_property_override(gneiss_type_registry registry,
                                           std::vector<prefab_property_override>& overrides,
                                           prefab_property_override candidate,
                                           prefab_property_value source_value) noexcept {
  auto operation = validate_prefab_property_override(registry, candidate);
  if (operation != GNEISS_SUCCESS) {
    return operation;
  }
  if (!valid_value(source_value) ||
      prefab_property_value_kind(source_value) != prefab_property_value_kind(candidate.value)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  normalize_zero(candidate.value);
  normalize_zero(source_value);
  try {
    const auto found = std::ranges::find(overrides, candidate.key, &prefab_property_override::key);
    if (candidate.value == source_value) {
      if (found != overrides.end()) {
        overrides.erase(found);
      }
      return GNEISS_SUCCESS;
    }
    if (found != overrides.end()) {
      found->value = std::move(candidate.value);
    } else {
      overrides.push_back(std::move(candidate));
      std::ranges::sort(overrides, [](const auto& left, const auto& right) {
        return prefab_property_override_key_less(left.key, right.key);
      });
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::scene_internal
