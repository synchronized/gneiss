// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include <string>
namespace e = gneiss::engine;
struct custom_description {
  inline static constexpr std::string_view type_name = "test_component";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({21, 1, 2, 3});
  bool enabled{};
};
std::span<const std::byte> bytes(std::string_view value) {
  return std::as_bytes(std::span{value.data(), value.size()});
}
e::result<std::vector<std::byte>, e::meta::serialization_diagnostic>
encode(const custom_description& value) {
  auto text = bytes(value.enabled ? R"({"enabled":true})" : R"({})");
  return std::vector<std::byte>(text.begin(), text.end());
}
e::result<custom_description, e::meta::serialization_diagnostic>
decode(std::span<const std::byte> value) {
  std::string_view text{reinterpret_cast<const char*>(value.data()), value.size()};
  if (text == R"({"enabled":true})")
    return custom_description{true};
  if (text == R"({})")
    return custom_description{false};
  return e::meta::serialization_diagnostic{
      .path = "/enabled", .message = "缺少布尔字段", .error = e::error::invalid_argument};
}
std::string scene(std::string_view components) {
  return std::string(
             R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":)") +
         std::string(components) + "}]}";
}
std::string entry(e::guid type, std::string_view content) {
  return std::string(R"({"$type_guid":")") + type.string() + R"(","$content":)" +
         std::string(content) + "}";
}
int main() try {
  if (e::initialize_meta([](const e::meta::registry_ref&) {
        return e::meta::type_builder<custom_description>{}
            .serialization<&encode, &decode>()
            .commit();
      }).failed())
    return 1;
  const auto custom = entry(custom_description::type_guid, R"({"enabled":true})");
  auto parsed = e::decode_scene(bytes(scene("[" + custom + "]")));
  if (!parsed || parsed->objects.front().components.size() != 1 ||
      !parsed->objects.front().components.get<custom_description>()->enabled ||
      parsed->objects.front().components.get<e::camera_description>())
    return 2;
  auto copy = *parsed;
  copy.objects.front().components.update<custom_description>(
      [](auto& value) { value.enabled = false; });
  if (!parsed->objects.front().components.get<custom_description>()->enabled)
    return 3;
  // false 编码省略 enabled，保存不能把原始 true 字段合并回来。
  auto encoded = e::encode_scene(copy);
  auto roundtrip =
      encoded ? e::decode_scene(*encoded)
              : e::result<e::scene_description, e::meta::serialization_diagnostic>{encoded.error()};
  if (!roundtrip || roundtrip->objects.front().components.get<custom_description>()->enabled)
    return 4;
  auto unknown = e::decode_scene(bytes(scene("[" + entry(e::guid::from_bytes({99}), "{}") + "]")));
  if (unknown || unknown.error().error != e::error::not_found ||
      unknown.error().path.find("/objects/0/components/0") != 0)
    return 5;
  auto duplicate = e::decode_scene(bytes(scene("[" + custom + "," + custom + "]")));
  if (duplicate || duplicate.error().error != e::error::invalid_argument)
    return 6;
  auto invalid = e::decode_scene(
      bytes(scene("[" + entry(custom_description::type_guid, R"({"enabled":null})") + "]")));
  if (invalid || invalid.error().path != "/objects/0/components/0/$content/enabled")
    return 7;
  if (e::decode_scene(bytes(scene("{}"))))
    return 8;
  auto owner = e::create_native();
  if (!owner)
    return 9;
  auto unsupported = e::create_scene_object(**owner, parsed->objects.front(), {}, {});
  if (unsupported || unsupported.error() != e::error::unsupported)
    return 10;
  const auto camera = entry(
      e::camera_description::type_guid,
      R"({"vertical_field_of_view_radians":1,"near_plane":0.1,"far_plane":100,"is_primary":false})");
  auto supported = e::decode_scene(bytes(scene("[" + camera + "]")));
  if (!supported)
    return 11;
  auto node = e::create_scene_object(**owner, supported->objects.front(), {}, {});
  if (!node)
    return 12;
  (void)(*owner)->object_destroy(*node);
  // Prefab 共用完全相同的组件协议。
  auto prefab_text = scene("[" + camera + "," + custom + "]");
  prefab_text.replace(prefab_text.find("gneiss.scene"), 12, "gneiss.prefab");
  prefab_text.replace(prefab_text.find("scene_uuid"), 10, "prefab_uuid");
  prefab_text.replace(prefab_text.find("\"version\":5"), 11, "\"version\":1");
  auto prefab = e::decode_prefab(bytes(prefab_text));
  if (!prefab || prefab->objects.front().components.size() != 2)
    return 13;
  auto prefab_bytes = e::encode_prefab(*prefab);
  if (!prefab_bytes || !e::decode_prefab(*prefab_bytes))
    return 14;
  return 0;
} catch (...) {
  return 99;
}
