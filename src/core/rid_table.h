// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RID_TABLE_H_
#define GNEISS_CORE_RID_TABLE_H_

#include <gneiss/core/result.h>
#include <gneiss/core/rid.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss::core {

enum class resource_type : std::uint8_t {
  invalid = 0,
  test_resource = 1,
  mesh = 2,
  material = 3,
  world = 4,
  application = 5,
  scene_instance = 6,
  texture = 7,
  type_registry = 8,
  game_context = 9,
};

namespace detail {

inline constexpr std::uint64_t slot_mask = (UINT64_C(1) << 24U) - 1U;
inline constexpr std::uint64_t generation_mask = (UINT64_C(1) << 16U) - 1U;
inline constexpr std::uint64_t type_mask = (UINT64_C(1) << 8U) - 1U;
inline constexpr std::uint32_t generation_shift = 24U;
inline constexpr std::uint32_t type_shift = 40U;
inline constexpr std::uint32_t domain_shift = 48U;

struct rid_parts {
  std::uint32_t slot;
  std::uint16_t generation;
  resource_type type;
  std::uint16_t domain;
};

[[nodiscard]] constexpr gneiss_rid encode_rid(rid_parts parts) noexcept {
  return static_cast<gneiss_rid>(parts.slot + 1U) |
         (static_cast<gneiss_rid>(parts.generation) << generation_shift) |
         (static_cast<gneiss_rid>(parts.type) << type_shift) |
         (static_cast<gneiss_rid>(parts.domain) << domain_shift);
}

[[nodiscard]] constexpr rid_parts decode_rid(gneiss_rid value) noexcept {
  const auto encoded_slot = static_cast<std::uint32_t>(value & slot_mask);
  return {
      encoded_slot == 0U ? std::numeric_limits<std::uint32_t>::max() : encoded_slot - 1U,
      static_cast<std::uint16_t>((value >> generation_shift) & generation_mask),
      static_cast<resource_type>((value >> type_shift) & type_mask),
      static_cast<std::uint16_t>(value >> domain_shift),
  };
}

} // namespace detail

/** 单个 Service 拥有的资源表；调用方负责外部同步。 */
template <typename Resource> class rid_table final {
public:
  explicit rid_table(std::uint16_t domain) noexcept : domain_(domain) {}

  rid_table(const rid_table&) = delete;
  rid_table& operator=(const rid_table&) = delete;
  rid_table(rid_table&&) = delete;
  rid_table& operator=(rid_table&&) = delete;

  template <typename Value>
  [[nodiscard]] gneiss_result create(resource_type type, Value&& value,
                                     gneiss_rid* out_rid) noexcept {
    if (out_rid == nullptr || domain_ == 0U || type == resource_type::invalid) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    *out_rid = GNEISS_NULL_RID;

    try {
      std::size_t index = 0;
      for (; index < slots_.size(); ++index) {
        if (!slots_[index].value.has_value() && !slots_[index].is_retired) {
          break;
        }
      }
      if (index == slots_.size()) {
        if (slots_.size() >= detail::slot_mask) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
        slots_.emplace_back();
      }

      auto& slot = slots_[index];
      slot.value.emplace(std::forward<Value>(value));
      slot.type = type;
      *out_rid =
          detail::encode_rid({static_cast<std::uint32_t>(index), slot.generation, type, domain_});
      return GNEISS_SUCCESS;
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  [[nodiscard]] Resource* get(gneiss_rid value, resource_type expected_type) noexcept {
    const auto parts = detail::decode_rid(value);
    if (!is_valid(parts, expected_type)) {
      return nullptr;
    }
    return &*slots_[parts.slot].value;
  }

  [[nodiscard]] const Resource* get(gneiss_rid value, resource_type expected_type) const noexcept {
    const auto parts = detail::decode_rid(value);
    if (!is_valid(parts, expected_type)) {
      return nullptr;
    }
    return &*slots_[parts.slot].value;
  }

  [[nodiscard]] gneiss_result destroy(gneiss_rid value, resource_type expected_type) noexcept {
    const auto parts = detail::decode_rid(value);
    if (!is_valid(parts, expected_type)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }

    auto& slot = slots_[parts.slot];
    slot.value.reset();
    slot.type = resource_type::invalid;
    if (slot.generation == std::numeric_limits<std::uint16_t>::max()) {
      // generation 耗尽后永久退役槽位，避免极端复用使历史 RID 再次有效。
      slot.is_retired = true;
    } else {
      ++slot.generation;
    }
    return GNEISS_SUCCESS;
  }

  [[nodiscard]] std::size_t live_count() const noexcept {
    std::size_t count = 0;
    for (const auto& slot : slots_) {
      count += slot.value.has_value() ? 1U : 0U;
    }
    return count;
  }

private:
  struct slot {
    std::optional<Resource> value;
    std::uint16_t generation = 1U;
    resource_type type = resource_type::invalid;
    bool is_retired = false;
  };

  [[nodiscard]] bool is_valid(detail::rid_parts parts, resource_type expected_type) const noexcept {
    if (parts.domain != domain_ || parts.type != expected_type || parts.slot >= slots_.size()) {
      return false;
    }
    const auto& slot = slots_[parts.slot];
    return slot.value.has_value() && slot.generation == parts.generation &&
           slot.type == expected_type;
  }

  std::uint16_t domain_;
  std::vector<slot> slots_;
};

} // namespace gneiss::core

#endif
