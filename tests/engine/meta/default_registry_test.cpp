// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <array>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/mesh/mesh_description.hpp>
#include <gneiss/engine/resource/scene/scene_description.hpp>
#include <gneiss/engine/core/meta/resolve.hpp>
#ifdef GNEISS_TEST_THREADS
#include <atomic>
#include <thread>
#include <vector>
#endif
#include <stdexcept>
#include <string_view>
namespace e = gneiss;
namespace m = e::meta;
struct sample {
  inline static constexpr std::string_view type_name = "default_registry_sample";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({91, 23, 3});
  float speed = 1;
};
// 内建类型的固定身份必须是 UUID v4，数学值与注册对象之间也不能重号。
constexpr std::array migrated_ids{e::type_identity<e::math::vector4>::guid(),
                                  e::type_identity<e::math::matrix3>::guid(),
                                  e::type_identity<e::math::matrix4>::guid(),
                                  e::value::type_guid,
                                  m::object::type_guid,
                                  e::material_parameters::type_guid,
                                  e::texture_sampling::type_guid,
                                  e::texture_description::type_guid,
                                  e::mesh_description::type_guid,
                                  e::input::action_binding::type_guid,
                                  e::input::action_definition::type_guid,
                                  e::component_description_base::type_guid,
                                  e::prefab_reference_description::type_guid,
                                  e::game_object_description::type_guid,
                                  e::type_identity<e::prefab_component_override>::guid(),
                                  e::prefab_property_override_key::type_guid,
                                  e::prefab_property_override::type_guid};
static_assert([] {
  for (std::size_t i = 0; i < migrated_ids.size(); ++i) {
    const auto& bytes = migrated_ids[i].bytes();
    if ((bytes[6] & 0xf0) != 0x40 || (bytes[8] & 0xc0) != 0x80)
      return false;
    for (std::size_t j = 0; j < i; ++j)
      if (migrated_ids[i] == migrated_ids[j])
        return false;
  }
  return true;
}());

static int calls = 0;
e::error register_project() {
  ++calls;
  // 内置类型已注册但尚未冻结；查询不得提前冻结。
  if (m::registered_types().error() != e::error::not_ready)
    return e::error::internal;
  return m::registration::type<sample>()
      .property<&sample::speed>(gneiss::property_id{1}, "speed")
      .commit();
}
e::error fail_registration() { return e::error::unsupported; }
e::error throw_registration() { throw std::runtime_error("注册失败"); }
int main(int argc, char** argv) {
  auto initial = m::default_registry();
  if (!initial || m::is_frozen() || !m::resolve<sample>())
    return 1;
  if (argc > 1) {
    const bool throws = std::string_view(argv[1]) == "throw";
    if (throws) {
      e::install_fatal_handler();
      static_cast<void>(e::initialize_meta(throw_registration));
      return 99;
    }
    const auto expected = e::error::unsupported;
    if (e::initialize_meta(fail_registration) != expected || e::initialize_meta() != expected ||
        m::is_frozen())
      return 2;
    return 0;
  }
  if (e::initialize_meta(register_project).failed() || calls != 1 || !m::is_frozen() ||
      e::initialize_meta().failed() ||
      e::initialize_meta(register_project) != e::error::invalid_state || calls != 1)
    return 3;
  auto again = m::default_registry();
  if (!again || again != initial || !m::resolve<sample>().registered() ||
      !m::resolve<e::math::transform>().registered() ||
      !m::resolve<e::camera_description>().registered() ||
      !m::resolve<e::material_description>().registered())
    return 4;
  sample object;
  auto view = m::const_instance::from_object(m::resolve<sample>(), &object);
  auto mutable_view = m::instance::from_object(m::resolve<sample>(), &object);
  if (!view || !mutable_view ||
      !view->template capability<gneiss::meta::cap::object>()->member("speed") ||
      !mutable_view->template capability<gneiss::meta::cap::object>()->member("speed") ||
      m::const_instance::from_object(m::resolve<sample>(), nullptr).error() !=
          e::error::invalid_argument)
    return 12;
  if (mutable_view->capability<m::cap::object>()->member("speed")->assign(e::value{2.0F}).failed())
    return 5;
  auto value = mutable_view->capability<m::cap::object>()->member("speed")->read_value();
  if (!value || std::get<float>(value->payload) != 2.0F || object.speed != 2.0F)
    return 6;
  auto encoded = gneiss::asset_file::serialize_bytes(e::material_description{});
  if (!encoded || !gneiss::asset_file::deserialize<e::material_description>(*encoded))
    return 7;
  if (m::register_type<sample>({.schema_version = 1}).ok() ||
      m::registration::type<sample>().commit().ok())
    return 8;
  auto isolated = m::create_registry();
  if (!isolated || isolated == initial ||
      m::register_type<sample>(isolated, {.schema_version = 1}).failed() ||
      m::freeze(isolated).failed() || m::registered_types(isolated)->size() != 1)
    return 9;
  // 默认引用释放后再次取得仍为同一张表；隔离注册不改变默认表。
  const auto count = m::registered_types()->size();
  again.reset();
  if (m::default_registry() != initial || m::registered_types()->size() != count)
    return 10;
#ifdef GNEISS_TEST_THREADS
  std::atomic<bool> good{true};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      auto shared = m::default_registry();
      if (!shared || shared != initial || e::initialize_meta().failed() ||
          !m::resolve<sample>().registered())
        good = false;
    });
  }
  for (auto& thread : threads)
    thread.join();
  if (!good)
    return 11;
#endif
  return 0;
}
