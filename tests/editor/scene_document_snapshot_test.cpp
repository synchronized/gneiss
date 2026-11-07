// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_document_snapshot.hpp"
#include "engine/function/meta_registration.hpp"
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
int main() {
  // 只注册类型；不创建 Application、World、执行器或文件服务。
  if (e::initialize_meta().failed())
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
  candidate.objects[0].prefab->components.push_back({{}, instance.uuid, {}});
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
  return 0;
}
