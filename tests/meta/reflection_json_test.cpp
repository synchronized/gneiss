// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/asset_decoder.hpp"
#include <future>
#include <limits>
#include <stdexcept>
namespace e = gneiss::engine;
namespace m = e::meta;
struct sample {
  inline static constexpr std::string_view type_name = "json_sample";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({61, 2});
  bool enabled = true;
  std::int64_t signed_number = -4;
  std::uint64_t unsigned_number = 5;
  float speed = 1;
  double precise = 2;
  std::string name = "默认";
  e::guid identity{};
  e::math::vector3 position{1, 2, 3};
  e::math::quaternion rotation{1, 0, 0, 0};
  float computed() const { return speed * 2; }
};
m::serialization_diagnostic validate_sample(const sample& object) {
  if (object.speed <= 0)
    return {.path = "/speed", .message = "速度必须大于零", .error = e::error::invalid_argument};
  return {};
}
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
e::error register_sample(const m::registry_ref& registry, std::string_view name = "name") {
  return m::type_builder<sample>{registry}
      .property<&sample::enabled>(m::property_id{1}, "enabled", m::property_flags::required)
      .property<&sample::signed_number>(m::property_id{2}, "signed")
      .property<&sample::unsigned_number>(m::property_id{3}, "unsigned")
      .property<&sample::speed>(m::property_id{4}, "speed")
      .property<&sample::precise>(m::property_id{5}, "precise")
      .property<&sample::name>(m::property_id{6}, name)
      .property<&sample::identity>(m::property_id{7}, "identity")
      .property<&sample::position>(m::property_id{8}, "position")
      .property<&sample::rotation>(m::property_id{9}, "rotation")
      .readonly<&sample::computed>(m::property_id{10}, "computed")
      .serialization()
      .validate<&validate_sample>()
      .commit();
}
struct tracked {
  inline static constexpr std::string_view type_name = "tracked_json";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({61, 3});
  inline static int alive = 0;
  tracked() { ++alive; }
  ~tracked() { --alive; }
  float value = 0;
};
e::error reject(const tracked&) { return e::error::invalid_argument; }
e::result<std::vector<std::byte>, m::serialization_diagnostic> custom_encode(const sample&) {
  return std::vector<std::byte>{std::byte{42}};
}
e::result<sample, m::serialization_diagnostic> custom_decode(std::span<const std::byte>) {
  return sample{};
}
bool nested_json_contract();
int main() try {
  if (!nested_json_contract())
    return 25;
  auto created = m::create_registry();
  if (!created)
    return 1;
  auto registry = *created;
  if (register_sample(registry).failed())
    return 2;
  if (m::serialize(registry, sample{}).error().error != e::error::not_ready)
    return 3;
  if (m::type_builder<tracked>{registry}
          .property<&tracked::value>(m::property_id{1}, "value")
          .serialization()
          .validate<&reject>()
          .commit()
          .failed() ||
      m::freeze(registry).failed())
    return 4;
  sample original;
  original.signed_number = std::numeric_limits<std::int64_t>::min();
  original.unsigned_number = std::numeric_limits<std::uint64_t>::max();
  original.name = std::string("中文\"\\");
  original.identity = sample::type_guid;
  original.rotation = {0.9F, 0.1F, 0.2F, 0.3F};
  auto encoded = m::serialize(registry, original);
  if (!encoded)
    return 5;
  const std::string_view text{reinterpret_cast<const char*>(encoded->data()), encoded->size()};
  if (text.find("computed") != std::string_view::npos)
    return 6;
  auto decoded = m::deserialize<sample>(registry, *encoded);
  if (!decoded || decoded->signed_number != original.signed_number ||
      decoded->unsigned_number != original.unsigned_number || decoded->name != original.name ||
      decoded->identity != original.identity || decoded->position != original.position ||
      decoded->rotation != original.rotation)
    return 7;
  auto defaults =
      m::deserialize<sample>(registry, bytes(R"({"enabled":false,"unknown":1,"computed":99})"));
  if (!defaults || defaults->enabled || defaults->name != "默认" || defaults->speed != 1)
    return 8;
  const auto fails = [&](std::string_view json, std::string_view path) {
    auto result = m::deserialize<sample>(registry, bytes(json));
    return !result && result.error().error == e::error::invalid_argument &&
           result.error().path == path;
  };
  if (!fails("{}", "/enabled") || !fails(R"({"enabled":null})", "/enabled") ||
      !fails(R"({"enabled":true,"enabled":false})", "/enabled") ||
      !fails(R"({"enabled":true,"speed":0})", "/speed") ||
      !fails(R"({"enabled":true,"speed":1e100})", "/speed") ||
      !fails(R"({"enabled":true,"unsigned":-1})", "/unsigned") ||
      !fails(R"({"enabled":true,"signed":18446744073709551615})", "/signed") ||
      !fails(R"({"enabled":true,"signed":2.5})", "/signed") ||
      !fails(R"({"enabled":true,"position":[1,2]})", "/position") ||
      !fails(R"({"enabled":true,"identity":"bad"})", "/identity"))
    return 9;
  original.speed = std::numeric_limits<float>::infinity();
  auto invalid = m::serialize(registry, original);
  // getter 产生非法属性值沿用 Meta 的 internal 语义，并保留字段位置。
  if (invalid || invalid.error().error != e::error::internal || invalid.error().path != "/speed")
    return 10;
  original.speed = -1;
  invalid = m::serialize(registry, original);
  if (invalid || invalid.error().path != "/speed")
    return 11;
  auto failed =
      m::deserialize_object(registry, e::runtime_type_of<tracked>(), bytes(R"({"value":1})"));
  if (failed || tracked::alive != 0)
    return 12;
  auto failed_field =
      m::deserialize_object(registry, e::runtime_type_of<tracked>(), bytes(R"({"value":"bad"})"));
  if (failed_field || tracked::alive != 0)
    return 13;
  // 同一个 C++ 类型在隔离注册表使用不同属性名，不能回退到默认注册表。
  auto other = m::create_registry();
  if (!other || register_sample(*other, "a/b~c").failed() || m::freeze(*other).failed())
    return 14;
  auto escaped = m::deserialize<sample>(*other, bytes(R"({"enabled":true,"a/b~c":2})"));
  if (escaped || escaped.error().path != "/a~1b~0c")
    return 15;
  // CPU 资源入口统一使用默认表；独立表不再由解码适配器持有。
  auto shared = m::default_registry();
  if (!shared || register_sample(*shared).failed() || m::freeze().failed())
    return 16;
  auto loader = e::detail::make_reflected_decoder<sample>(4096);
  auto weak = std::weak_ptr<e::meta::type_registry_state>(*other);
  other->reset();
  if (!weak.expired() || !loader || !(*loader)->decode(*encoded))
    return 17;
  auto task = std::async(std::launch::async, [&] {
    return static_cast<bool>(m::deserialize<sample>(registry, *encoded));
  });
  if (!m::deserialize<sample>(registry, *encoded) || !task.get())
    return 18;
  auto custom = m::create_registry();
  if (!custom ||
      m::type_builder<sample>{*custom}
          .serialization()
          .serialization<&custom_encode, &custom_decode>()
          .commit()
          .failed() ||
      m::freeze(*custom).failed())
    return 19;
  auto custom_output = m::serialize(*custom, sample{});
  if (!custom_output || custom_output->size() != 1 || custom_output->front() != std::byte{42} ||
      !m::deserialize<sample>(*custom, {}))
    return 20;
  auto fallback = m::create_registry();
  if (!fallback ||
      m::type_builder<sample>{*fallback}
          .property<&sample::enabled>(m::property_id{1}, "enabled")
          .serialization<&custom_encode, nullptr>()
          .serialization()
          .commit()
          .failed() ||
      m::freeze(*fallback).failed())
    return 22;
  auto fallback_decoded = m::deserialize<sample>(*fallback, bytes(R"({"enabled":false})"));
  if (!fallback_decoded || fallback_decoded->enabled)
    return 23;
  auto invalid_flags = m::create_registry();
  if (!invalid_flags || m::type_builder<sample>{*invalid_flags}
                                .property<&sample::enabled>(m::property_id{1}, "enabled",
                                                            m::property_flags::read_only |
                                                                m::property_flags::required)
                                .serialization()
                                .commit() != e::error::invalid_argument)
    return 24;
  return 0;
} catch (...) {
  return 99;
}
