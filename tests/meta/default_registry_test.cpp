// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/material/material_description.hpp"
#ifdef GNEISS_TEST_THREADS
#include <atomic>
#include <thread>
#include <vector>
#endif
#include <stdexcept>
#include <string_view>
namespace e = gneiss::engine;
namespace m = e::meta;
struct sample {
  inline static constexpr std::string_view type_name = "default_registry_sample";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({91, 23, 3});
  float speed = 1;
};
static int calls = 0;
e::error register_project(const m::registry_ref& registry) {
  ++calls;
  auto shared = m::default_registry();
  if (!shared || *shared != registry)
    return e::error::internal;
  // 内置类型已注册但尚未冻结；查询不得提前冻结。
  if (m::type_count().error() != e::error::not_ready)
    return e::error::internal;
  return m::type_builder<sample>{}.property<&sample::speed>(m::property_id{1}, "speed").commit();
}
e::error fail_registration(const m::registry_ref&) { return e::error::unsupported; }
e::error throw_registration(const m::registry_ref&) { throw std::runtime_error("注册失败"); }
int main(int argc, char** argv) {
  auto initial = m::default_registry();
  if (!initial || !m::is_frozen() || *m::is_frozen() ||
      m::describe_type<sample>().error() != e::error::not_ready)
    return 1;
  if (argc > 1) {
    const bool throws = std::string_view(argv[1]) == "throw";
    const auto expected = throws ? e::error::internal : e::error::unsupported;
    if (e::initialize_meta(throws ? throw_registration : fail_registration) != expected ||
        e::initialize_meta() != expected || *m::is_frozen())
      return 2;
    return 0;
  }
  if (e::initialize_meta(register_project).failed() || calls != 1 || !*m::is_frozen() ||
      e::initialize_meta().failed() ||
      e::initialize_meta(register_project) != e::error::invalid_state || calls != 1)
    return 3;
  auto again = m::default_registry();
  if (!again || *again != *initial || !m::describe_type<sample>() ||
      !m::describe_type<e::math::transform>() || !m::describe_type<e::camera_properties>() ||
      !m::describe_type<e::material_description>())
    return 4;
  sample object;
  if (m::write_property(sample::type_guid, m::property_id{1}, &object, e::value{2.0F}).failed())
    return 5;
  auto value = m::read_property(sample::type_guid, m::property_id{1}, &object);
  if (!value || std::get<float>(value->payload) != 2.0F || object.speed != 2.0F)
    return 6;
  auto encoded = m::serialize(e::material_description{});
  if (!encoded || !m::deserialize<e::material_description>(*encoded))
    return 7;
  if (m::register_type<sample>({.schema_version = 1}).ok() ||
      m::type_builder<sample>{}.commit().ok())
    return 8;
  auto isolated = m::create_registry();
  if (!isolated || *isolated == *initial ||
      m::register_type<sample>(*isolated, {.schema_version = 1}).failed() ||
      m::freeze(*isolated).failed() || *m::type_count(*isolated) != 1)
    return 9;
  // 默认引用释放后再次取得仍为同一张表；隔离注册不改变默认表。
  const auto count = *m::type_count();
  again->reset();
  if (*m::default_registry() != *initial || *m::type_count() != count)
    return 10;
#ifdef GNEISS_TEST_THREADS
  std::atomic<bool> good{true};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      auto shared = m::default_registry();
      if (!shared || *shared != *initial || e::initialize_meta().failed() ||
          !m::describe_type<sample>())
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
