// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registration.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/world/world_component_access.hpp"
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
struct movement_description {
  inline static constexpr std::string_view type_name = "test_movement";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 1});
  float speed = 1;
  movement_mode mode = movement_mode::walk;
  movement_settings settings{};
  std::vector<float> samples{1, 2};
  bool operator==(const movement_description&) const = default;
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
inline std::uint64_t entity(e::world& world, gneiss::scene_node_id node) {
  gneiss::entity_id value;
  if (world.node_get_entity(node, value).failed())
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
        state(c.owner).emplace_or_replace<movement_component>(entity(c.owner, c.node), d);
        return e::error::success;
      },
      .capture = [](e::world& w,
                    gneiss::scene_node_id n) -> e::result<std::optional<movement_description>> {
        auto* value = state(w).get<movement_component>(entity(w, n));
        return value ? std::optional{value->data} : std::nullopt;
      },
      .remove =
          [](e::world& w, gneiss::scene_node_id n) {
            return state(w).remove<movement_component>(entity(w, n));
          },
      .equal = [](const movement_description& a, const movement_description& b) { return a == b; }};
}

inline e::error register_movement_meta() {
  auto status = e::meta::enum_builder<movement_mode>{}
                    .value("Walk", movement_mode::walk)
                    .value("Run", movement_mode::run)
                    .commit();
  if (status.failed())
    return status;
  status = e::meta::type_builder<movement_settings>{}
               .property<&movement_settings::acceleration>(e::meta::property_id{1}, "acceleration")
               .property<&movement_settings::maximum>(e::meta::property_id{2}, "maximum")
               .serialization()
               .commit();
  if (status.failed())
    return status;
  return e::meta::type_builder<movement_description>{}
      .property<&movement_description::speed>(e::meta::property_id{1}, "speed")
      .property<&movement_description::mode>(e::meta::property_id{2}, "mode")
      .property<&movement_description::settings>(e::meta::property_id{3}, "settings")
      .property<&movement_description::samples>(e::meta::property_id{4}, "samples")
      .serialization()
      .commit();
}
}
