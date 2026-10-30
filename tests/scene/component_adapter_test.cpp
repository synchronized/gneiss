// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>
namespace e = gneiss::engine;
namespace {
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("组件适配测试失败，行=" + std::to_string(location.line()));
}
struct movement_description {
  inline static constexpr std::string_view type_name = "test_movement";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 1});
  float speed = 1;
};
struct movement_component {
  movement_description data;
  std::shared_ptr<int> lifetime = std::make_shared<int>(0);
};
struct unknown_description {
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 2});
};
e::world_state& state(e::world& world) {
  auto access = e::detail::world_component_access::get(world);
  check(access.has_value());
  return **access;
}
std::uint64_t entity(e::world& world, gneiss::scene_node_id node) {
  gneiss::entity_id value;
  check(world.node_get_entity(node, value).ok());
  return value.get();
}
e::component_operations<movement_description> operations() {
  return {.validate = [](const movement_description& d) -> e::error {
            return d.speed >= 0 ? e::error::success : e::error::invalid_argument;
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
          .equal = [](const movement_description& a,
                      const movement_description& b) { return a.speed == b.speed; }};
}
e::prepared_scene_description prepare(e::scene_description scene, e::prefab_asset_ref prefab = {}) {
  e::scene_description_inputs input;
  input.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(std::move(scene))};
  if (prefab)
    input.prefabs.emplace("asset://test.prefab", std::move(prefab));
  e::prepared_scene_description output;
  e::scene_diagnostic diagnostic;
  check(e::prepare_loaded_scene(input, {}, output, diagnostic, {}).ok());
  check(output.assets.empty());
  return output;
}
}
int main() try {
  check(e::initialize_meta([] {
          return e::meta::type_builder<movement_description>{}
              .property<&movement_description::speed>(e::meta::property_id{1}, "speed")
              .serialization()
              .commit();
        }).ok());
  check(e::register_scene_component(e::component_adapter{}) == e::error::invalid_argument);
  check(e::register_scene_component(operations()).ok());
  check(e::register_scene_component(operations()) == e::error::invalid_argument);
  auto owner = e::create_native();
  check(owner.has_value());
  auto& world = **owner;
  auto files = std::make_shared<e::files::service>();
  e::asset_manager assets(files);
  e::resource_service resources(assets);
  e::scene_instances scenes(world, resources);
  e::scene_description source;
  source.uuid = e::guid::from_bytes({1});
  e::object_description first;
  first.uuid = e::guid::from_bytes({2});
  first.components.set(movement_description{2});
  source.objects.push_back(first);
  auto second = first;
  second.uuid = e::guid::from_bytes({3});
  second.parent_uuid = first.uuid;
  source.objects.push_back(second);
  // 真实文件格式解码后进入统一准备与同步实例化路径。
  auto encoded = e::encode_scene(source);
  check(encoded.has_value());
  auto decoded = e::decode_scene(*encoded);
  check(decoded.has_value());
  auto prepared = prepare(*decoded);
  check(e::register_scene_component(operations()) == e::error::invalid_state);
  gneiss::scene_instance_id instance;
  check(scenes.instantiate_prepared(std::move(prepared), {}, instance).ok());
  gneiss::scene_node_id node;
  check(scenes.find_node(instance, first.uuid, node).ok());
  auto id = entity(world, node);
  auto* component = state(world).get<movement_component>(id);
  check(component && component->data.speed == 2);
  component->data.speed = 4;
  check(e::edit_runtime_component({world, node}, movement_description::type_guid,
                                  e::meta::property_id{1}, e::value{5.0F})
            .ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  check(e::edit_runtime_component({world, node}, movement_description::type_guid,
                                  e::meta::property_id{1},
                                  e::value{-1.0F}) == e::error::invalid_argument);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  e::scene_description saved;
  check(scenes.capture_description(instance, saved).ok());
  check(saved.objects[0].components.get<movement_description>()->speed == 5);
  auto saved_bytes = e::encode_scene(saved);
  check(saved_bytes.has_value());
  auto restored = e::decode_scene(*saved_bytes);
  check(restored.has_value());
  state(world).get<movement_component>(id)->data.speed = 8;
  check(scenes.reload_loaded(instance, *restored).ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  // 第二个安装失败：第一个对象的已应用修改也必须恢复。
  auto failing = *restored;
  failing.objects[0].components.set(movement_description{7});
  failing.objects[1].components.set(movement_description{13});
  check(scenes.reload_loaded(instance, failing) == e::error::invalid_state);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  e::error foreign;
  std::thread worker([&] {
    auto captured = e::capture_components(world, node);
    foreign = captured ? e::error::success : captured.error();
  });
  worker.join();
  check(foreign == e::error::invalid_state);
  check(!e::capture_components(world, {}));
  e::component_descriptions unknown;
  unknown.set(unknown_description{});
  check(e::validate_components(unknown) == e::error::unsupported);
  std::weak_ptr<int> lifetime = state(world).get<movement_component>(id)->lifetime;
  restored->objects[0].components = {};
  check(scenes.reload_loaded(instance, *restored).ok());
  check(!state(world).get<movement_component>(id) && lifetime.expired());
  check(scenes.reload_loaded(instance, saved).ok());
  lifetime = state(world).get<movement_component>(id)->lifetime;
  e::scene_subtree_snapshot snapshot;
  check(scenes.capture_subtree(instance, node, snapshot).ok());
  check(scenes.destroy_subtree(instance, node).ok());
  check(lifetime.expired());
  check(scenes.restore_subtree(instance, snapshot, {}, {}, node).ok());
  id = entity(world, node);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  lifetime = state(world).get<movement_component>(id)->lifetime;
  check(scenes.unload(instance).ok());
  check(lifetime.expired());
  // Prefab 覆盖使用同一适配入口，不认识测试组件的类型。
  auto prefab = std::make_shared<e::prefab_description>();
  prefab->uuid = e::guid::from_bytes({4});
  prefab->root_uuid = first.uuid;
  prefab->objects = source.objects;
  e::object_description reference;
  reference.uuid = e::guid::from_bytes({5});
  reference.prefab.emplace();
  reference.prefab->uri = "asset://test.prefab";
  reference.prefab->overrides.push_back({.key = {.source_node_uuid = first.uuid,
                                                 .type_guid = movement_description::type_guid,
                                                 .property_id = e::meta::property_id{1}},
                                         .value = e::value{9.0F}});
  source.objects = {reference};
  e::prefab_asset_ref prefab_ref{prefab};
  check(scenes.instantiate_prepared(prepare(source, prefab_ref), {}, instance).ok());
  check(scenes.find_node(instance, reference.uuid, node).ok());
  id = entity(world, node);
  check(state(world).get<movement_component>(id)->data.speed == 9);
  auto next = std::make_shared<e::prefab_description>(*prefab);
  next->objects[0].components.set(movement_description{3});
  check(
      scenes.reload_prefab_loaded(instance, "asset://test.prefab", e::prefab_asset_ref{next}).ok());
  check(state(world).get<movement_component>(id)->data.speed == 9);
  check(scenes.capture_description(instance, saved).ok());
  check(saved.objects[0].components.empty());
  check(saved.objects[0].prefab->overrides.size() == 1);
  lifetime = state(world).get<movement_component>(id)->lifetime;
  check(scenes.unload(instance).ok());
  check(lifetime.expired());
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
