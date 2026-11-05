// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/result.hpp"
#include <string>
namespace gneiss::engine::meta {
class instance;
class mutable_instance;
}
namespace gneiss::engine::serialization {
/** 独立属性快照；复制不共享可写对象。仅向调用方提供的临时目标恢复。 */
struct property_snapshot final {
  static constexpr std::string_view type_name = "property_snapshot";
  static constexpr guid type_guid = guid::literal("bd87ec88-e621-4dda-b883-ce9bbd032415");
  std::string json;
  bool operator==(const property_snapshot&) const = default;
};
[[nodiscard]] GNEISS_API result<property_snapshot> capture_property(const meta::instance&) noexcept;
[[nodiscard]] GNEISS_API error restore_property(const meta::mutable_instance&,
                                                const property_snapshot&) noexcept;
}
