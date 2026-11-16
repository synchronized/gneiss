// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "editor/documents/prefab_edit.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/prefab_property_override.hpp>

#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace {

const auto source_a = *gneiss::guid::parse("20000000-0000-4000-8000-000000000001");
const auto source_b = *gneiss::guid::parse("20000000-0000-4000-8000-000000000002");

[[nodiscard]] gneiss::prefab_property_override
make_translation(gneiss::guid source_node, gneiss::math::vector3 value) {
  return {.key = {.target = {.source_node_uuid = source_node,
                             .target_type_guid = gneiss::math::transform::type_guid},
                  .path = {gneiss::meta::transform_translation_field}},
          .operation = gneiss::value{.payload = value}};
}

} // namespace

int main() {
  if (gneiss::initialize_meta(gneiss::test_support::register_movement_meta).failed())
    return 1;
  if (gneiss::register_scene_component(gneiss::test_support::operations()).failed())
    return 1;

  // 编辑回调异常必须传播，候选的修改不得污染来源或组件集合。
  {
    using namespace gneiss;
    using gneiss::test_support::movement_description;
    component_descriptions components;
    components.set(movement_description{1});
    const auto original = *components.find(movement_description::type_guid);
    bool propagated = false;
    try {
      (void)components.update<movement_description>([](auto& candidate) {
        candidate.speed = 99;
        throw std::bad_alloc{};
      });
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    if (!propagated || components.get<movement_description>()->speed != 1 ||
        *components.find(movement_description::type_guid) != original)
      return 180;
    propagated = false;
    try {
      (void)modify_component(original, [](meta::instance candidate) -> error {
        auto speed = candidate.member(meta::property_id{1});
        if (!speed || speed->assign(value{99.0F}).failed())
          return error::invalid_argument;
        throw std::runtime_error("测试编辑回调异常");
      });
    } catch (const std::runtime_error&) {
      propagated = true;
    }
    if (!propagated || original->get<movement_description>()->speed != 1)
      return 181;
    if (components.update<movement_description>([](auto& candidate) { candidate.speed = 2; })
            .failed() ||
        components.get<movement_description>()->speed != 2)
      return 182;
  }

  // 同一地址跨插入、删除其他元素和重排仍定位原 UUID；丢失和重号明确失败。
  {
    using namespace gneiss;
    scene_description scene;
    game_object_description a, b, c;
    a.uuid = source_a;
    a.name = "a";
    b.uuid = source_b;
    b.name = "b";
    c.uuid = guid::literal("212e2248-11bb-4565-a0c7-1e15b965a3ad");
    scene.objects = {a, b};
    const meta::property_path path{meta::property_id{2}, source_b, meta::property_id{2}};
    const auto read = [&]() -> result<value> {
      auto root = meta::view(std::as_const(scene));
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
    auto root = meta::view(std::as_const(scene));
    const meta::property_path index{meta::property_id{2}, std::size_t{0}};
    if (meta::resolve_path(*root, index, true).error() != error::unsupported)
      return 46;
    if (!meta::resolve_path(*root, index))
      return 47;
    std::map<std::string, float> mapping{{"a/b", 3}};
    auto map = meta::view(mapping);
    const meta::property_path map_path{std::string("a/b")};
    auto leaf = meta::resolve_path(*map, map_path, true);
    if (!leaf || leaf->assign(value{4.0F}).failed() || mapping["a/b"] != 4)
      return 48;
  }
  // 实际 Prefab 列表通过稳定身份重排、删除、新增；文件保持操作顺序。
  {
    using namespace gneiss;
    using gneiss::test_support::movement_description;
    const auto component_id = guid::generate();
    const auto third = guid::literal("49386fe2-09cb-42d8-92de-eeb021178b4b");
    movement_description original;
    original.waypoints = {{source_a, 1}, {source_b, 2}};
    auto changed = original;
    changed.waypoints = {{source_b, 20}, {third, 3}};
    auto a = meta::view(std::as_const(original)), b = meta::view(std::as_const(changed));
    auto before = serialization::capture_value(*a), after = serialization::capture_value(*b);
    if (!before || !after)
      return 70;
    auto edits = diff_values(*before, *after);
    if (!edits || edits->size() != 3)
      return 71;
    prefab_reference_description reference{.uri = "asset://waypoints.prefab", .overrides = {}};
    for (const auto& edit : *edits)
      reference.overrides.push_back(
          {{{source_b, movement_description::type_guid, component_id}, edit.path}, edit.operation});
    auto encoded = serialization::json::encode(reference);
    if (!encoded)
      return 72;
    auto decoded =
        serialization::json::decode<prefab_reference_description>(std::string_view{*encoded});
    if (!decoded)
      return 73;
    prefab_description source;
    source.root_uuid = source_a;
    game_object_description root, child;
    root.uuid = source_a;
    child.uuid = source_b;
    child.parent.object = source_a;
    original.uuid = component_id;
    if (child.components.replace({make_component_description(original)}).failed())
      return 79;
    source.objects = {root, child};
    prefab_description output;
    if (prepare_prefab_reference(source, *decoded, output).failed() ||
        output.objects[1].components.get<movement_description>()->waypoints != changed.waypoints ||
        source.objects[1].components.get<movement_description>()->waypoints != original.waypoints)
      return 74;
    // 后一条编辑允许定位前一条 insert 刚创建的元素。
    decoded->overrides.push_back(
        {.key = {.target = {source_b, movement_description::type_guid, component_id},
                 .path = {meta::property_id{5}, changed.waypoints.back().uuid,
                          meta::property_id{2}}},
         .operation = value{42.0F}});
    if (prepare_prefab_reference(source, *decoded, output).failed() ||
        output.objects[1].components.get<movement_description>()->waypoints.back().distance != 42)
      return 75;
  }
  // 来源变化保留无效覆盖；同一组件内的独立编辑仍然生效。
  {
    using namespace gneiss;
    using gneiss::test_support::movement_description;
    constexpr auto element = guid::literal("ced4a670-7729-4f65-911d-aebfd4b7400a");
    constexpr auto missing = guid::literal("6b929783-6346-4ce7-ad7c-a7dd143a602b");
    prefab_description source;
    source.root_uuid = source_a;
    game_object_description root, child;
    root.uuid = source_a;
    child.uuid = source_b;
    child.parent.object = source_a;
    movement_description movement;
    movement.waypoints = {{element, 1}};
    child.components.set(movement);
    source.objects = {root, child};
    prefab_reference_description reference;
    const prefab_property_target target{source_b, movement_description::type_guid,
                                        child.components.identity(movement_description::type_guid)};
    reference.overrides = {
        {{target, {meta::property_id{5}}}, move_value_operation{element, missing}},
        {{target, {meta::property_id{5}, element, meta::property_id{2}}}, value{7.0F}},
        {{target, {meta::property_id{5}}}, erase_value_operation{missing}},
        {{{missing, movement_description::type_guid}, {meta::property_id{1}}}, value{8.0F}},
        {{target, {meta::property_id{1}}}, value{4.0F}}};
    auto replayed = replay_prefab_reference(source, reference);
    if (!replayed || replayed->conflicts.size() != 2 || replayed->conflicts[0].index != 0 ||
        replayed->conflicts[1].index != 3 || replayed->conflicts[0].diagnostic.path.empty() ||
        reference.overrides.size() != 5)
      return 76;
    const auto* current = replayed->description.objects[1].components.get<movement_description>();
    if (current->speed != 4 || current->waypoints[0].distance != 7 ||
        source.objects[1].components.get<movement_description>()->speed != 1)
      return 77;
    // 同一组件跨字段约束失败，只保留来源组件；另一对象的修改仍然生效。
    reference.overrides = {{{target, {meta::property_id{3}, meta::property_id{1}}}, value{1000.0F}},
                           make_translation(source_b, {8, 0, 0})};
    replayed = replay_prefab_reference(source, reference);
    if (!replayed || replayed->conflicts.size() != 1 || replayed->conflicts[0].index != 0 ||
        replayed->description.objects[1].transform.translation.x != 8 ||
        replayed->description.objects[1]
                .components.get<movement_description>()
                ->settings.acceleration != 1)
      return 78;
  }
  // 快照复制不共享原数据；列表长度可改变，应用失败不修改输出。
  {
    using namespace gneiss;
    using gneiss::test_support::movement_description;
    movement_description movement;
    auto view = meta::view(std::as_const(movement));
    auto samples = view->member(meta::property_id{4});
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
    game_object_description root, child;
    root.uuid = source_a;
    child.uuid = source_b;
    child.parent.object = source_a;
    child.components.set(movement_description{});
    source.objects = {root, child};
    prefab_description output;
    change.key.target.component_uuid = child.components.identity(movement_description::type_guid);
    std::vector<prefab_property_override> changes;
    if (set_prefab_property_override(changes, change, *before).failed())
      return 51;
    if (prepare_prefab_description(source, changes, output).failed())
      return 52;
    if (output.objects[1].components.get<movement_description>()->samples !=
            std::vector<float>{8, 9, 10} ||
        source.objects[1].components.get<movement_description>()->samples.size() != 2)
      return 53;
    auto bytes = serialization::json::encode_bytes(changes.front());
    if (!bytes)
      return 54;
    auto copy = serialization::json::decode<prefab_property_override>(*bytes);
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
    using namespace gneiss;
    auto original = make_translation(source_a, {1, 2, 3});
    original.key.path = {meta::property_id{2}, std::size_t{3}, std::string("a/b"), source_b};
    auto bytes = gneiss::serialization::json::encode_bytes(original);
    if (!bytes)
      return 30;
    auto copy = gneiss::serialization::json::decode<prefab_property_override>(*bytes);
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
    using namespace gneiss;
    auto invalid = make_translation(source_a, {1, 2, 3});
    invalid.operation = value{ref<meta::object>{}};
    if (validate_prefab_property_override(invalid) != error::invalid_argument)
      return 33;
  }

  // 共享计算保留其他节点覆盖；恢复原值删除稀疏记录，失败不修改输入。
  {
    using namespace gneiss;
    const mesh_component_description original{"asset://base.mesh", "asset://base.material"};
    const mesh_component_description replacement{"asset://next.mesh", "asset://next.material"};
    const std::vector existing{make_translation(source_b, {1, 2, 3})};
    auto changed = gneiss::editor::prepare_mesh_component_overrides(source_a, source_b, original,
                                                                    replacement, existing);
    if (!changed || changed->size() != 3 || existing.size() != 1)
      return 20;
    auto restored = gneiss::editor::prepare_mesh_component_overrides(source_a, source_b, original,
                                                                     original, *changed);
    if (!restored || restored->size() != 1 ||
        restored->front().key.target.source_node_uuid != source_b)
      return 21;
    auto invalid = gneiss::editor::prepare_mesh_component_overrides({}, source_b, original,
                                                                    replacement, existing);
    if (invalid || invalid.error() != error::invalid_argument || existing.size() != 1)
      return 22;
  }

  std::vector<gneiss::prefab_property_override> overrides;
  const gneiss::value source{.payload = gneiss::math::vector3{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(source_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::validate_prefab_property_override(first) != gneiss::error::success ||
      gneiss::set_prefab_property_override(overrides, second, source) !=
          gneiss::error::success ||
      gneiss::set_prefab_property_override(overrides, first, source) !=
          gneiss::error::success ||
      overrides.size() != 2U || overrides[0].key.target.source_node_uuid != source_b ||
      overrides[1].key.target.source_node_uuid != source_a) {
    return 2;
  }

  auto updated = make_translation(source_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::set_prefab_property_override(overrides, updated, source) !=
          gneiss::error::success ||
      overrides.size() != 2U ||
      std::get<gneiss::math::vector3>(overrides[1].replacement().payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(source_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::set_prefab_property_override(overrides, restored, source) !=
          gneiss::error::success ||
      overrides.size() != 1U || overrides[0].key.target.source_node_uuid != source_b) {
    return 4;
  }

  auto wrong_type = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  wrong_type.operation = gneiss::value{1.0F};
  auto invalid_address = make_translation({}, {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(source_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::validate_prefab_property_override(wrong_type) !=
          gneiss::error::invalid_argument ||
      gneiss::validate_prefab_property_override(invalid_address) !=
          gneiss::error::invalid_argument ||
      gneiss::validate_prefab_property_override(non_finite) !=
          gneiss::error::invalid_argument) {
    return 5;
  }

  gneiss::prefab_property_override read_only{
      .key = {.target = {.source_node_uuid = source_a,
                         .target_type_guid = gneiss::camera_description::type_guid},
              .path = {gneiss::meta::property_id{4}}},
      .operation = gneiss::value{.payload = true}};
  if (gneiss::validate_prefab_property_override(read_only) !=
      gneiss::error::invalid_argument) {
    return 6;
  }

  auto missing = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  missing.key.path.front() = gneiss::meta::property_id{999};
  if (gneiss::validate_prefab_property_override(missing) !=
      gneiss::error::not_found) {
    return 7;
  }

  return 0;
}
