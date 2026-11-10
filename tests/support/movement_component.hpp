// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "value_result.hpp"
#include <stdexcept>
namespace gneiss::test_support {
namespace e = gneiss::engine;
enum class movement_mode { walk, run };
struct movement_settings {
  inline static constexpr std::string_view type_name = "test_movement_settings";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 3});
  float acceleration = 1;
  float maximum = 10;
  bool operator==(const movement_settings&) const = default;
};
struct waypoint {
  static constexpr std::string_view type_name = "test_waypoint";
  static constexpr e::guid type_guid = e::guid::literal("84ea93d2-1d22-4342-9970-d2b48c16f246");
  e::guid uuid;
  float distance{};
  bool operator==(const waypoint&) const = default;
};
}
namespace gneiss::engine::meta {
template <> struct sequence_identity_traits<std::vector<gneiss::test_support::waypoint>> {
  static constexpr bool enabled = true;
  static guid identity(const gneiss::test_support::waypoint& v) { return v.uuid; }
};
}
namespace gneiss::test_support {
struct movement_description
    : e::meta::object_type<movement_description, e::component_description_base> {
  movement_description() = default;
  explicit movement_description(float value) : speed(value) {}
  inline static constexpr std::string_view type_name = "test_movement";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 1});
  float speed = 1;
  movement_mode mode = movement_mode::walk;
  movement_settings settings{};
  std::vector<float> samples{1, 2};
  std::vector<waypoint> waypoints;
  bool operator==(const movement_description& other) const {
    return speed == other.speed && mode == other.mode && settings == other.settings &&
           samples == other.samples && waypoints == other.waypoints;
  }
};
struct movement_component {
  movement_description data;
  std::shared_ptr<int> lifetime = std::make_shared<int>(0);
};
inline e::world_state& state(e::world& world) {
  auto access = e::detail::world_component_access::get(world);
  if (!access)
    throw std::runtime_error("World 访问失败");
  return **access;
}
inline std::uint64_t entity(e::world& world, gneiss::entity_id node) {
  gneiss::entity_id value;
  if (gneiss::test_support::assign_value(world.object(gneiss::entity_id{(node).get()}), value)
          .failed())
    throw std::runtime_error("节点访问失败");
  return value.get();
}
inline e::component_operations<movement_description> operations() {
  return {
      .validate = [](const movement_description& d) -> e::error {
        return d.speed >= 0 && d.settings.acceleration <= d.settings.maximum
                   ? e::error::success
                   : e::error::invalid_argument;
      },
      .install = [](e::component_context& c, const movement_description& d) -> e::error {
        // 模拟运行期安装失败，验证已应用的其他对象能恢复。
        if (d.speed == 13)
          return e::error::invalid_state;
        state(*c.target.owner()).emplace_or_replace<movement_component>(c.target.entity().get(), d);
        return e::error::success;
      },
      .capture = [](e::game_object target) -> e::result<std::optional<movement_description>> {
        auto* value = state(*target.owner()).get<movement_component>(target.entity().get());
        return value ? std::optional{value->data} : std::nullopt;
      },
      .remove =
          [](e::game_object target) {
            return state(*target.owner()).remove<movement_component>(target.entity().get());
          },
      .equal = [](const movement_description& a, const movement_description& b) { return a == b; },
      .properties = e::component_property_operations<movement_description>{}};
}

inline e::error register_movement_meta() {
  auto status = e::meta::registration::enumeration<movement_mode>()
                    .value("Walk", movement_mode::walk)
                    .value("Run", movement_mode::run)
                    .commit();
  if (status.failed())
    return status;
  status = e::meta::registration::type<movement_settings>()
               .property<&movement_settings::acceleration>(e::meta::property_id{1}, "acceleration")
               .property<&movement_settings::maximum>(e::meta::property_id{2}, "maximum")
               .commit();
  if (status.failed())
    return status;
  status = e::meta::registration::type<waypoint>()
               .property<&waypoint::uuid>(e::meta::property_id{1}, "uuid")
               .property<&waypoint::distance>(e::meta::property_id{2}, "distance")
               .commit();
  if (status.failed())
    return status;
  return e::meta::registration::type<movement_description>()
      .base<e::component_description_base>()
      .property<&movement_description::speed>(e::meta::property_id{1}, "speed")
      .property<&movement_description::mode>(e::meta::property_id{2}, "mode")
      .property<&movement_description::settings>(e::meta::property_id{3}, "settings")
      .property<&movement_description::samples>(e::meta::property_id{4}, "samples")
      .property<&movement_description::waypoints>(e::meta::property_id{5}, "waypoints")
      .commit();
}
}
