// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

using namespace gneiss::engine;

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

[[nodiscard]] bool valid_value(const gneiss::engine::prefab_property_value& value) noexcept {
  if (value.payload.valueless_by_exception()) {
    return false;
  }
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

void normalize_zero(gneiss::engine::prefab_property_value& value) noexcept {
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

namespace gneiss::engine {

result validate_prefab_property_override(const meta::registry_ref& registry,
                                         const prefab_property_override& value) noexcept try {
  if (!engine::is_valid_prefab_author_address(value.key.node) || !value.key.type_id ||
      !value.key.field_id.is_valid() || !valid_value(value.value)) {
    return result::invalid_argument;
  }
  meta::property_info field;
  const auto found =
      meta::describe_property(registry, value.key.type_id, value.key.field_id, field);
  if (found.failed()) {
    return static_cast<result>(found);
  }
  if (field.kind != engine::prefab_property_value_kind(value.value) ||
      !has_flags(field.capabilities, meta::property_capabilities::writable)) {
    return result::invalid_argument;
  }
  return result::success;
} catch (...) {
  return result::internal;
}

result set_prefab_property_override(const meta::registry_ref& registry,
                                    std::vector<prefab_property_override>& overrides,
                                    prefab_property_override candidate,
                                    prefab_property_value source_value) noexcept {
  auto operation = validate_prefab_property_override(registry, candidate);
  if (operation != result::success) {
    return operation;
  }
  if (!valid_value(source_value) || engine::prefab_property_value_kind(source_value) !=
                                        engine::prefab_property_value_kind(candidate.value)) {
    return result::invalid_argument;
  }
  normalize_zero(candidate.value);
  normalize_zero(source_value);
  try {
    const auto found = std::ranges::find(overrides, candidate.key, &prefab_property_override::key);
    if (candidate.value == source_value) {
      if (found != overrides.end()) {
        overrides.erase(found);
      }
      return result::success;
    }
    if (found != overrides.end()) {
      found->value = std::move(candidate.value);
    } else {
      overrides.push_back(std::move(candidate));
      std::ranges::sort(overrides, [](const auto& left, const auto& right) {
        return engine::prefab_property_override_key_less(left.key, right.key);
      });
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::engine
