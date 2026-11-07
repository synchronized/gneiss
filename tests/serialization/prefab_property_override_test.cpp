// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace {

const auto source_a = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001");
const auto source_b = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000002");

[[nodiscard]] gneiss::engine::prefab_property_override
make_translation(gneiss::engine::guid source_node, gneiss::engine::math::vector3 value) {
  return {.key = {.target = {.source_node_uuid = source_node,
                             .target_type_guid = gneiss::engine::transform_component_type},
                  .path = {gneiss::engine::transform_translation_field}},
          .operation = gneiss::engine::value{.payload = value}};
}

} // namespace

int main() {
  if (gneiss::engine::initialize_meta(gneiss::test_support::register_movement_meta).failed())
    return 1;
  if (gneiss::engine::register_scene_component(gneiss::test_support::operations()).failed())
    return 1;

  // 同一地址跨插入、删除其他元素和重排仍定位原 UUID；丢失和重号明确失败。
  {
    using namespace gneiss::engine;
    scene_description scene;
    object_description a, b, c;
    a.uuid = source_a;
    a.name = "a";
    b.uuid = source_b;
    b.name = "b";
    c.uuid = guid::literal("212e2248-11bb-4565-a0c7-1e15b965a3ad");
    scene.objects = {a, b};
    const meta::property_path path{meta::property_id{2}, source_b, meta::property_id{2}};
    const auto read = [&]() -> result<value> {
      auto root = meta::instance::from(scene);
      if (!root)
        return root.error();
      auto leaf = meta::resolve_path(*root, path, true);
      return leaf ? leaf->read_value() : result<value>{leaf.error()};
    };
    if (!read() || std::get<std::string>(read()->payload) != "b")
      return 40;
    scene.objects.insert(scene.objects.begin(), c);
    std::ranges::reverse(scene.objects);
    if (!read() || std::get<std::string>(read()->payload) != "b")
      return 41;
    std::erase_if(scene.objects, [&](const auto& v) { return v.uuid == source_a; });
    if (!read())
      return 42;
    scene.objects.push_back(b);
    if (read().error() != error::invalid_argument)
      return 43;
    scene.objects.pop_back();
    std::erase_if(scene.objects, [&](const auto& v) { return v.uuid == source_b; });
    if (read().error() != error::not_found)
      return 44;
    scene.objects.push_back(b);
    scene.objects.front().uuid = {};
    if (read().error() != error::invalid_argument)
      return 45;
    auto root = meta::instance::from(scene);
    const meta::property_path index{meta::property_id{2}, std::size_t{0}};
    if (meta::resolve_path(*root, index, true).error() != error::unsupported)
      return 46;
    if (!meta::resolve_path(*root, index))
      return 47;
    std::map<std::string, float> mapping{{"a/b", 3}};
    auto map = meta::mutable_instance::from(mapping);
    const meta::property_path map_path{std::string("a/b")};
    auto leaf = meta::resolve_path(*map, map_path, true);
    if (!leaf || leaf->write_value(value{4.0F}).failed() || mapping["a/b"] != 4)
      return 48;
  }
  // 实际 Prefab 列表通过稳定身份重排、删除、新增；文件保持操作顺序。
  {
    using namespace gneiss::engine;
    using gneiss::test_support::movement_description;
    const auto third = guid::literal("49386fe2-09cb-42d8-92de-eeb021178b4b");
    movement_description original;
    original.waypoints = {{source_a, 1}, {source_b, 2}};
    auto changed = original;
    changed.waypoints = {{source_b, 20}, {third, 3}};
    auto a = meta::instance::from(original), b = meta::instance::from(changed);
    auto before = serialization::capture_value(*a), after = serialization::capture_value(*b);
    if (!before || !after)
      return 70;
    auto edits = diff_values(*before, *after);
    if (!edits || edits->size() != 3)
      return 71;
    prefab_reference_description reference{.uri = "asset://waypoints.prefab", .overrides = {}};
    for (const auto& edit : *edits)
      reference.overrides.push_back(
          {{{source_b, movement_description::type_guid}, edit.path}, edit.operation});
    auto encoded = serialization::json::serialize(reference);
    if (!encoded)
      return 72;
    auto decoded =
        serialization::json::deserialize<prefab_reference_description>(std::string_view{*encoded});
    if (!decoded)
      return 73;
    prefab_description source;
    source.root_uuid = source_a;
    object_description root, child;
    root.uuid = source_a;
    child.uuid = source_b;
    child.parent_uuid = source_a;
    child.components.set(original);
    source.objects = {root, child};
    prefab_description output;
    if (prepare_prefab_reference(source, *decoded, output).failed() ||
        output.objects[1].components.get<movement_description>()->waypoints != changed.waypoints ||
        source.objects[1].components.get<movement_description>()->waypoints != original.waypoints)
      return 74;
    // 后一条编辑允许定位前一条 insert 刚创建的元素。
    decoded->overrides.push_back({
        .key = {.target = {source_b, movement_description::type_guid},
                .path = {meta::property_id{5}, changed.waypoints.back().uuid, meta::property_id{2}}},
        .operation = value{42.0F}});
    if (prepare_prefab_reference(source, *decoded, output).failed() ||
        output.objects[1].components.get<movement_description>()->waypoints.back().distance != 42)
      return 75;
  }
  // 快照复制不共享原数据；列表长度可改变，应用失败不修改输出。
  {
    using namespace gneiss::engine;
    using gneiss::test_support::movement_description;
    movement_description movement;
    auto view = meta::instance::from(movement);
    auto samples = view->property(meta::property_id{4});
    auto before = serialization::capture_value(*samples);
    movement.samples = {8, 9, 10};
    auto after = serialization::capture_value(*samples);
    if (!before || !after)
      return 50;
    prefab_property_override change{.key = {.target = {source_b, movement_description::type_guid},
                                            .path = {meta::property_id{4}}},
                                    .operation = *after};
    movement.samples.clear();
    prefab_description source;
    source.root_uuid = source_a;
    object_description root, child;
    root.uuid = source_a;
    child.uuid = source_b;
    child.parent_uuid = source_a;
    child.components.set(movement_description{});
    source.objects = {root, child};
    prefab_description output;
    std::vector<prefab_property_override> changes;
    if (set_prefab_property_override(changes, change, *before).failed())
      return 51;
    if (prepare_prefab_description(source, changes, output).failed())
      return 52;
    if (output.objects[1].components.get<movement_description>()->samples !=
            std::vector<float>{8, 9, 10} ||
        source.objects[1].components.get<movement_description>()->samples.size() != 2)
      return 53;
    auto bytes = serialization::json::serialize_bytes(changes.front());
    if (!bytes)
      return 54;
    auto copy = serialization::json::deserialize<prefab_property_override>(*bytes);
    if (!copy || copy->operation != changes.front().operation)
      return 54;
    changes.front().operation = value{sequence_value{sequence_value_data{{value{true}}}}};
    if (prepare_prefab_description(source, changes, output).ok() ||
        output.objects[1].components.get<movement_description>()->samples.size() != 3)
      return 55;
    changes.front() = change;
    changes.front().key.path.emplace_back(std::size_t{1});
    changes.front().operation = value{2.0F};
    if (validate_prefab_property_override(changes.front()) != error::unsupported)
      return 56;
    changes = {change};
    if (set_prefab_property_override(changes, change, *after).failed() || !changes.empty())
      return 57;
  }

  {
    using namespace gneiss::engine;
    auto original = make_translation(source_a, {1, 2, 3});
    original.key.path = {meta::property_id{2}, std::size_t{3}, std::string("a/b"), source_b};
    auto bytes = gneiss::engine::serialization::json::serialize_bytes(original);
    if (!bytes)
      return 30;
    auto copy = gneiss::engine::serialization::json::deserialize<prefab_property_override>(*bytes);
    if (!copy || copy->key != original.key || copy->operation != original.operation)
      return 31;
    const std::string json(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    if (json.find(R"("$kind":"property")") == std::string::npos ||
        json.find(R"("$kind":"index")") == std::string::npos ||
        json.find(R"("$kind":"key")") == std::string::npos ||
        json.find(R"("$kind":"element")") == std::string::npos ||
        json.find(R"("key":)") == std::string::npos)
      return 32;
  }

  {
    using namespace gneiss::engine;
    auto invalid = make_translation(source_a, {1, 2, 3});
    invalid.operation = value{ref<meta::object>{}};
    if (validate_prefab_property_override(invalid) != error::invalid_argument)
      return 33;
  }

  // 共享计算保留其他节点覆盖；恢复原值删除稀疏记录，失败不修改输入。
  {
    using namespace gneiss::engine;
    const mesh_renderer_description original{"asset://base.mesh", "asset://base.material"};
    const mesh_renderer_description replacement{"asset://next.mesh", "asset://next.material"};
    const std::vector existing{make_translation(source_b, {1, 2, 3})};
    auto changed = prepare_mesh_renderer_overrides(source_a, original, replacement, existing);
    if (!changed || changed->size() != 3 || existing.size() != 1)
      return 20;
    auto restored = prepare_mesh_renderer_overrides(source_a, original, original, *changed);
    if (!restored || restored->size() != 1 ||
        restored->front().key.target.source_node_uuid != source_b)
      return 21;
    auto invalid = prepare_mesh_renderer_overrides({}, original, replacement, existing);
    if (invalid || invalid.error() != error::invalid_argument || existing.size() != 1)
      return 22;
  }

  std::vector<gneiss::engine::prefab_property_override> overrides;
  const gneiss::engine::value source{.payload = gneiss::engine::math::vector3{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(source_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(first) != gneiss::engine::error::success ||
      gneiss::engine::set_prefab_property_override(overrides, second, source) !=
          gneiss::engine::error::success ||
      gneiss::engine::set_prefab_property_override(overrides, first, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 2U || overrides[0].key.target.source_node_uuid != source_b ||
      overrides[1].key.target.source_node_uuid != source_a) {
    return 2;
  }

  auto updated = make_translation(source_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(overrides, updated, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 2U ||
      std::get<gneiss::engine::math::vector3>(overrides[1].replacement().payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(source_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(overrides, restored, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 1U || overrides[0].key.target.source_node_uuid != source_b) {
    return 4;
  }

  auto wrong_type = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  wrong_type.operation = gneiss::engine::value{1.0F};
  auto invalid_address = make_translation({}, {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(source_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(wrong_type) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(invalid_address) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(non_finite) !=
          gneiss::engine::error::invalid_argument) {
    return 5;
  }

  gneiss::engine::prefab_property_override read_only{
      .key = {.target = {.source_node_uuid = source_a,
                         .target_type_guid = gneiss::engine::camera_component_type},
              .path = {gneiss::engine::meta::property_id{4}}},
      .operation = gneiss::engine::value{.payload = true}};
  if (gneiss::engine::validate_prefab_property_override(read_only) !=
      gneiss::engine::error::invalid_argument) {
    return 6;
  }

  auto missing = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  missing.key.path.front() = gneiss::engine::meta::property_id{999};
  if (gneiss::engine::validate_prefab_property_override(missing) !=
      gneiss::engine::error::not_found) {
    return 7;
  }

  return 0;
}
