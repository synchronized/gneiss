// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/meta_registration.hpp"
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
struct linked_description
    : e::meta::object_type<linked_description, e::component_description_base> {
  static constexpr std::string_view type_name = "linked_description";
  static constexpr e::guid type_guid = e::guid::literal("ac5b637b-73a2-4e1a-b59b-3998a283c7ba");
  e::value links;
};
e::error register_links() {
  return e::meta::type_builder<linked_description>{}
      .base<e::component_description_base>()
      .property<&linked_description::links>(e::meta::property_id{1}, "links")
      .serialization()
      .commit();
}
int main() {
  // 只注册类型；不创建 Application、World、执行器或文件服务。
  if (e::initialize_meta(register_links).failed())
    return 1;
  auto source = e::make_ref<e::prefab_description>();
  source->uuid = e::guid::literal("131b05a9-2d9d-4e60-ae28-0e99c9772537");
  source->root_uuid = e::guid::literal("c068d77a-9c2c-4096-a712-71f120bbecb4");
  e::object_description root;
  root.uuid = source->root_uuid;
  root.name = "source";
  root.components.set(e::camera_description{{1.0F, 0.1F, 100.0F}});
  source->objects.push_back(root);
  e::scene_instantiation_data input;
  input.description.uuid = e::guid::literal("0a8fbc96-0ef7-4462-8d8c-16f807cb10cc");
  e::object_description instance;
  instance.uuid = e::guid::literal("36fdba8d-ae65-45aa-bcd3-f14fb58dbdb2");
  instance.prefab = e::prefab_reference_description{"asset://camera.prefab.json", {}, {}};
  input.description.objects.push_back(instance);
  e::prefab_instantiation_data prefab;
  prefab.resource = source;
  input.prefabs.emplace(instance.prefab->uri, std::move(prefab));
  auto frozen = ed::make_scene_document_snapshot("asset://scene.json", input);
  if (!frozen)
    return 2;
  const auto before = frozen->content;
  const auto camera_id = root.components.identity(e::camera_description::type_guid);
  if (!camera_id ||
      frozen->prefabs.at(instance.prefab->uri).content.get().objects[0].components[0].uuid !=
          camera_id)
    return 60;
  input.description.objects.clear();
  source->objects[0].name = "changed";
  source->objects[0].components.set(e::camera_description{{2.0F, 0.2F, 200.0F}});
  auto restored = ed::make_scene_document_inputs(*frozen);
  if (!restored || restored->scene->objects.size() != 1 ||
      restored->prefabs.at(instance.prefab->uri)->objects[0].name != "source" ||
      restored->prefabs.at(instance.prefab->uri)
              ->objects[0]
              .components.get<e::camera_description>()
              ->settings.fov != 1.0F)
    return 3;
  auto prepared = ed::prepare_scene_document(*frozen, {});
  if (!prepared || !prepared->missing.empty() || prepared->snapshot.content != before ||
      prepared->snapshot.prefabs != frozen->prefabs)
    return 4;
  auto invalid = *frozen;
  invalid.content = {};
  if (ed::make_scene_document_inputs(invalid))
    return 5;
  if (frozen->content != before)
    return 6;
  // 场景身份和层级直接由领域字段表达，候选修改不改变旧版本。
  auto candidate = before.get();
  if (candidate.uuid != input.description.uuid || candidate.objects[0].uuid != instance.uuid)
    return 7;
  candidate.objects[0].name = "renamed";
  auto renamed = e::restore_scene_record(e::scene_record{candidate});
  if (!renamed || renamed->objects[0].name != "renamed" ||
      before.get().objects[0].name == "renamed")
    return 8;
  candidate.objects[0].parent = instance.uuid;
  if (e::restore_scene_record(e::scene_record{candidate}))
    return 9;
  auto prefab_candidate = frozen->prefabs.at(instance.prefab->uri).content.get();
  prefab_candidate.objects[0].components[0].type = instance.uuid;
  if (e::restore_prefab_record(e::prefab_record{prefab_candidate}))
    return 10;
  prefab_candidate = frozen->prefabs.at(instance.prefab->uri).content.get();
  prefab_candidate.objects[0].components.push_back(prefab_candidate.objects[0].components[0]);
  if (e::restore_prefab_record(e::prefab_record{prefab_candidate}))
    return 11;
  candidate = before.get();
  candidate.objects[0].prefab->uri = "invalid";
  if (e::restore_scene_record(e::scene_record{candidate}))
    return 12;
  candidate = before.get();
  candidate.objects[0].prefab->components.push_back({{}, {}, instance.uuid, {}});
  if (e::restore_scene_record(e::scene_record{candidate}))
    return 13;
  // 即使绕过文件解析，也不能把可变对象引用带入不可变版本。
  e::prefab_property_override invalid_override;
  invalid_override.key.target.source_node_uuid = source->root_uuid;
  invalid_override.key.target.target_type_guid = e::camera_description::type_guid;
  invalid_override.key.path = {e::meta::property_id{1}};
  invalid_override.operation =
      e::set_value_operation{e::value{e::make_ref<e::camera_description>()}};
  auto unsafe = *restored->scene;
  unsafe.objects[0].prefab->overrides.push_back(std::move(invalid_override));
  if (e::capture_scene_record(unsafe))
    return 14;
  // 使用真实反射捕获结果验证嵌套引用，不只测试手工拼装的值树。
  const auto document_id = input.description.uuid;
  const auto target_document = e::guid::literal("fe555990-f953-42ab-9d34-f1ef120b61d1");
  const auto copied_id = e::guid::literal("07f7d2b6-e031-4e48-b7c3-5639c71e1de6");
  const auto external_id = source->root_uuid;
  e::object_description linked;
  linked.uuid = instance.uuid;
  linked_description links;
  const e::value internal{e::object_reference_value{document_id, linked.uuid}};
  const e::value external{e::object_reference_value{document_id, external_id}};
  const e::value foreign{e::object_reference_value{source->uuid, linked.uuid}};
  const auto map_link = [](e::value link) {
    e::map_value_data data;
    data.entries.push_back({e::value{std::string{"target"}}, std::move(link)});
    return e::value{e::map_value{data}};
  };
  e::sequence_value_data link_values;
  link_values.elements = {internal,
                          external,
                          foreign,
                          e::value{linked.uuid},
                          e::value{e::optional_value{e::optional_value_data{internal}}},
                          e::value{e::variant_value{e::variant_value_data{"selected", internal}}},
                          map_link(internal)};
  links.links = e::value{e::sequence_value{link_values}};
  linked.components.set(links);
  auto original = e::capture_scene_record(e::scene_description{document_id, {linked}});
  if (!original)
    return 15;
  const std::array mapping{e::uuid_mapping{linked.uuid, copied_id}};
  auto copied = e::remap_scene_record(*original, target_document, mapping);
  if (!copied)
    return 16;
  auto decoded_copy = e::restore_scene_record(*copied);
  auto decoded_original = e::restore_scene_record(*original);
  if (!decoded_copy || !decoded_original || decoded_copy->uuid != target_document ||
      decoded_copy->objects[0].uuid != copied_id ||
      decoded_original->objects[0].components.get<linked_description>()->links != links.links)
    return 17;
  if (decoded_copy->objects[0].components.identity(linked_description::type_guid) ==
      decoded_original->objects[0].components.identity(linked_description::type_guid))
    return 61;
  auto expected = std::get<e::sequence_value>(links.links.payload).get();
  const e::value remapped{e::object_reference_value{target_document, copied_id}};
  expected.elements[0] = remapped;
  expected.elements[4] = e::value{e::optional_value{e::optional_value_data{remapped}}};
  expected.elements[5] = e::value{e::variant_value{e::variant_value_data{"selected", remapped}}};
  expected.elements[6] = map_link(remapped);
  if (decoded_copy->objects[0].components.get<linked_description>()->links !=
      e::value{e::sequence_value{expected}})
    return 18;
  if (e::remap_scene_record(*original, target_document, {}) ||
      e::remap_scene_record(*original, {}, mapping))
    return 19;
  auto bad_mapping = mapping;
  bad_mapping[0].source_uuid = external_id;
  if (e::remap_scene_record(*original, target_document, bad_mapping))
    return 20;
  auto tree = original->get();
  e::object_record child;
  child.uuid = external_id;
  child.parent = linked.uuid;
  child.prefab.emplace();
  child.prefab->uri = "asset://camera.prefab.json";
  child.prefab->overrides.push_back(
      {{.target = {source->root_uuid, e::camera_description::type_guid},
        .path = {e::meta::property_id{1}}},
       e::set_value_operation{internal}});
  tree.objects.push_back(child);
  const auto copied_child = e::guid::literal("68656d91-8dfd-4c01-a36f-02706e98510a");
  std::array mappings{mapping[0], e::uuid_mapping{external_id, copied_child}};
  auto tree_copy = e::remap_scene_record(e::scene_record{tree}, target_document, mappings);
  if (!tree_copy || tree_copy->get().objects[1].parent != copied_id ||
      tree_copy->get().objects[1].prefab->overrides[0].replacement() != remapped ||
      tree_copy->get().objects[1].prefab->overrides[0].key.target.source_node_uuid !=
          source->root_uuid)
    return 21;
  mappings[1].target_uuid = copied_id;
  if (e::remap_scene_record(e::scene_record{tree}, target_document, mappings))
    return 22;
  mappings[1] = mapping[0];
  if (e::remap_scene_record(e::scene_record{tree}, target_document, mappings))
    return 23;
  return 0;
}
