// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/scene/scene_description.hpp>

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <type_traits>

static_assert(!std::is_convertible_v<gneiss::rid, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::rid>);
static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::scene_instance_id>);

namespace {
bool scene_object_storage() {
  using namespace gneiss;
  using namespace gneiss;
  auto created = create_native();
  auto other = create_native();
  if (!created || !other)
    return false;
  auto& owner = **created;
  auto object_scene = owner.create_scene();
  if (!object_scene)
    return false;
  auto root = object_scene->create_object({"父节点", {}});
  if (!root)
    return false;
  auto child = object_scene->create_object({"子节点", {}}, *root);
  if (!child)
    return false;
  entity_id child_entity;
  if (gneiss::test_support::assign_value(child, child_entity).failed())
    return false;
  if (child->name() != "子节点" || child->set_name("新名称").failed() || child->name() != "新名称")
    return false;
  if (owner.find(child->entity()).try_component<gneiss::mesh_component>() != nullptr)
    return false;
  auto objects = object_scene->objects();
  auto roots = object_scene->roots();
  auto children = root->children();
  if (objects.size() != 2 || roots.size() != 1 || roots.front() != *root ||
      !children || children->size() != 1 || children->front() != *child)
    return false;
  if (child->try_component<mesh_component>() || child->add_component<mesh_component>().failed() ||
      !child->try_component<mesh_component>() ||
      child->add_component<mesh_component>() != error::invalid_state ||
      child->remove_component<mesh_component>().failed() ||
      child->try_component<mesh_component>() ||
      child->remove_component<mesh_component>() != error::not_found)
    return false;
  error add_from_worker;
  std::thread add_worker([&] { add_from_worker = child->add_component<mesh_component>(); });
  add_worker.join();
  if (add_from_worker != error::invalid_state)
    return false;
  math::transform invalid;
  invalid.scale.x = 0;
  auto failed = object_scene->create_object({"失败", invalid}, *root);
  auto count = owner.entity_count();
  if (failed || count != 2)
    return false;
  if ((*other)->find(root->entity()).destroy() != error::invalid_handle)
    return false;
  error foreign;
  std::thread worker([&] { foreign = (*root).destroy(); });
  worker.join();
  if (foreign != error::invalid_state)
    return false;
  if ((*root).destroy().failed())
    return false;
  if (object_scene->contains(*root) || object_scene->contains({}) ||
      !object_scene->objects().empty() || !object_scene->roots().empty())
    return false;
  count = owner.entity_count();
  if (count != 0 || owner.find(child_entity).is_valid())
    return false;
  if (children->front().is_valid() ||
      child->add_component<mesh_component>() != error::invalid_handle || child->is_valid() ||
      (*root).destroy() != error::invalid_handle)
    return false;
  auto replacement = object_scene->create_object({"新节点", {}});
  return replacement && *replacement != *root && *replacement != *child;
}

bool native_operations() {
  using namespace gneiss;
  gneiss::world_owner owner;
  if (gneiss::test_support::assign_value(gneiss::create_native(), owner) !=
      gneiss::error::success)
    return false;
  entity_id entity{};
  entity_id node{};
  auto object_scene = owner->create_scene();
  if (!object_scene)
    return false;
  auto object = object_scene->create_object({"object", {}});
  if (!object)
    return false;
  entity = object->entity();
  node = entity_id{entity.get()};
  if (!owner->find(entity).is_valid() || owner->entity_count() != 1)
    return false;
  gneiss::math::transform local;
  local.translation = {2, 3, 4};
  gneiss::math::transform actual;
  if (owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).set_local_transform(local) !=
          gneiss::error::success ||
      gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).world_transform(),
          actual) != gneiss::error::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()})
              .set_local_transform(invalid) != gneiss::error::invalid_argument ||
      gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).local_transform(),
          actual) != gneiss::error::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{0}).get()}).local_transform(), actual) !=
          gneiss::error::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::error foreign{};
  std::thread worker([&] { foreign = owner->find(gneiss::entity_id{entity}).destroy(); });
  worker.join();
  if (foreign != gneiss::error::invalid_state || !owner->find(entity).is_valid())
    return false;
  owner.reset();
  return !owner;
}

}

int main(int argc, char** argv) {
  if (argc == 2) {
    // 保留引擎默认断言诊断，仅在 abort 信号处结束子进程，避免交互崩溃弹窗。
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, [](int) { std::_Exit(86); });
    auto owner = gneiss::create_native();
    if (!owner)
      return 1;
    const std::string_view mode{argv[1]};
    if (mode != "count" && mode != "flush")
      return 2;
    std::thread worker([&] {
      if (mode == "count")
        (void)(*owner)->entity_count();
      else
        (*owner)->flush_changes();
    });
    worker.join();
    return 3;
  }
  if (gneiss::initialize_meta().failed())
    return 22;
  if (!scene_object_storage())
    return 21;
  if (!native_operations())
    return 20;
  namespace service = gneiss;
  gneiss::world_owner world{};
  if (gneiss::test_support::assign_value(gneiss::create_native(), world) !=
      gneiss::error::success) {
    return 1;
  }
  auto scene = world->create_scene();
  if (!scene)
    return 2;
  auto created_state = scene->create_object();
  if (!created_state)
    return 2;
  auto created_object = *created_state;
  const auto entity = created_object.entity().get();
  if (world->entity_count() != 1U) {
    return 3;
  }
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (created_object.add_component<gneiss::camera_component>(camera, false).failed()) {
    return 4;
  }
  service::math::camera_settings settings;
  if (!(settings = created_object.try_component<gneiss::camera_component>()->settings).valid() ||
      settings.near_plane != camera.near_plane || settings.far_plane != camera.far_plane) {
    return 5;
  }
  settings.far_plane = settings.near_plane;
  auto* camera_component = created_object.try_component<gneiss::camera_component>();
  camera_component->settings = settings;
  gneiss::math::matrix4 projection{1.0F};
  const auto original_projection = projection;
  if (camera_component->settings.valid() ||
      gneiss::math::perspective_rh_zo(camera_component->settings, 1.0F, projection) !=
          gneiss::error::invalid_argument || projection != original_projection) {
    return 6;
  }
  camera_component->settings = camera;
  gneiss::error wrong_thread{};
  if (gneiss::initialize_meta().failed())
    return 14;
  gneiss::math::transform candidate_transform;
  gneiss::value proposed;
  proposed.payload = gneiss::math::vector3{2, 3, 4};
  if (gneiss::meta::write_property(
          service::math::transform::type_guid, gneiss::meta::transform_translation_field,
          &candidate_transform, proposed) != gneiss::error::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload = gneiss::math::vector3{0, 1, 1};
  if (gneiss::meta::write_property(
          service::math::transform::type_guid, gneiss::meta::transform_scale_field,
          &candidate_transform, proposed) != gneiss::error::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_description candidate_camera{{1, 10, 20}, false};
  proposed.payload = 30.0F;
  if (gneiss::meta::write_property(
          gneiss::meta::type_of<service::math::camera_settings>().guid(),
          gneiss::meta::property_id{2}, &candidate_camera.settings,
          proposed) != gneiss::error::success) {
    return 17;
  }
  proposed.payload = 40.0F;
  auto camera_type = gneiss::meta::type_of<service::math::camera_settings>();
  if (gneiss::meta::write_property(
          camera_type.guid(),
          camera_type.find_property("far").id(), &candidate_camera.settings,
          proposed) != gneiss::error::success ||
      candidate_camera.settings.near_plane != 30 || candidate_camera.settings.far_plane != 40) {
    return 18;
  }
  // 借用只在拥有者存活期使用；移动拥有者不改变对象地址。
  service::world_owner native;
  if (gneiss::test_support::assign_value(service::create_native(), native).failed())
    return 14;
  const auto* original = native.get();
  auto independent = service::create_native();
  if (!independent || independent->get() == original || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (native || moved.get() != original || moved->entity_count() != 0)
    return 16;
  std::thread worker([&] {
    auto found = moved->object(gneiss::entity_id{entity});
    wrong_thread = found ? service::error::success : found.error();
  });
  worker.join();
  if (wrong_thread != service::error::invalid_state)
    return 17;
  moved.reset();
  return 0;
}
