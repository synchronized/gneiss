// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include <string>
namespace e = gneiss::engine;
struct reflected_description
    : e::meta::object_type<reflected_description, e::component_description_base> {
  inline static constexpr std::string_view type_name = "reflected_component";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({21, 1, 2, 4});
  float speed = 1;
};
std::size_t decode_calls = 0;
struct custom_description
    : e::meta::object_type<custom_description, e::component_description_base> {
  inline static constexpr std::string_view type_name = "test_component";
  inline static constexpr e::guid type_guid = e::guid::from_bytes({21, 1, 2, 3});
  custom_description() = default;
  explicit custom_description(bool value) : enabled(value) {}
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
  ++decode_calls;
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
  if (e::initialize_meta([]() {
        auto status = e::meta::type_builder<reflected_description>{}
                          .base<e::component_description_base>()
                          .property<&reflected_description::speed>(e::meta::property_id{1}, "speed")
                          .serialization()
                          .commit();
        if (status.failed())
          return status;
        return e::meta::type_builder<custom_description>{}
            .base<e::component_description_base>()
            .serialization<&encode, &decode>()
            .commit();
      }).failed())
    return 1;
  // 项目组件只注册字段，不写 codec；Scene/Prefab 的 GUID 分派共用默认表。
  const auto reflected = entry(reflected_description::type_guid, R"({"speed":3})");
  auto reflected_scene =
      e::meta::deserialize<e::scene_description>(bytes(scene("[" + reflected + "]")));
  if (!reflected_scene ||
      reflected_scene->objects.front().components.get<reflected_description>()->speed != 3)
    return 30;
  e::prefab_description reflected_prefab;
  reflected_prefab.uuid = reflected_scene->uuid;
  reflected_prefab.objects = reflected_scene->objects;
  reflected_prefab.root_uuid = reflected_prefab.objects.front().uuid;
  auto reflected_prefab_bytes = e::meta::serialize(reflected_prefab);
  if (!reflected_prefab_bytes)
    return 31;
  auto restored_prefab = e::meta::deserialize<e::prefab_description>(*reflected_prefab_bytes);
  if (!restored_prefab ||
      restored_prefab->objects.front().components.get<reflected_description>()->speed != 3)
    return 32;
  const auto custom = entry(custom_description::type_guid, R"({"enabled":true})");
  auto parsed = e::meta::deserialize<e::scene_description>(bytes(scene("[" + custom + "]")));
  if (!parsed || parsed->objects.front().components.size() != 1 ||
      !parsed->objects.front().components.get<custom_description>()->enabled ||
      parsed->objects.front().components.get<e::camera_description>())
    return 2;
  // 组件属性只能整项替换；失败不修改已有组件或共享快照。
  auto guarded = parsed->objects.front();
  auto object_view = e::meta::mutable_instance::from(guarded);
  if (!object_view)
    return 37;
  auto property = object_view->property("components");
  if (!property || !property->as_sequence() || !property->as_sequence()->supports_replacement() ||
      property->as_sequence()->at(0).error() != e::error::unsupported ||
      property->as_sequence()->resize(2) != e::error::unsupported ||
      !property->as_const().as_sequence())
    return 38;
  auto collection_bytes = e::meta::serialize(guarded.components);
  if (!collection_bytes || !e::meta::deserialize<e::component_descriptions>(*collection_bytes))
    return 41;
  auto original = *guarded.components.begin();
  auto null_status = e::meta::deserialize_instance(*property, bytes("[null]"));
  auto duplicate_status =
      e::meta::deserialize_instance(*property, bytes("[" + custom + "," + custom + "]"));
  if (null_status.error.ok() || duplicate_status.error.ok() || guarded.components.size() != 1 ||
      *guarded.components.begin() != original)
    return 39;
  if (e::meta::deserialize_instance(*property, bytes("[]")).error.failed() ||
      !guarded.components.empty() || parsed->objects.front().components.empty())
    return 40;
  auto copy = *parsed;
  copy.objects.front().components.update<custom_description>(
      [](auto& value) { value.enabled = false; });
  if (!parsed->objects.front().components.get<custom_description>()->enabled)
    return 3;
  // false 编码省略 enabled，保存不能把原始 true 字段合并回来。
  const auto scene_decodes = decode_calls;
  auto encoded = e::meta::serialize(copy);
  // 保存只调用编码器，不能通过反向解码产生新的组件对象或副作用。
  if (decode_calls != scene_decodes)
    return 15;
  auto roundtrip =
      encoded ? e::meta::deserialize<e::scene_description>(*encoded)
              : e::result<e::scene_description, e::meta::serialization_diagnostic>{encoded.error()};
  if (!roundtrip || roundtrip->objects.front().components.get<custom_description>()->enabled)
    return 4;
  auto unknown = e::meta::deserialize<e::scene_description>(
      bytes(scene("[" + entry(e::guid::from_bytes({99}), "{}") + "]")));
  if (unknown || unknown.error().error != e::error::not_found ||
      unknown.error().path.find("/objects/0/components/0") != 0)
    return 5;
  auto duplicate =
      e::meta::deserialize<e::scene_description>(bytes(scene("[" + custom + "," + custom + "]")));
  if (duplicate || duplicate.error().error != e::error::invalid_argument)
    return 6;
  auto invalid = e::meta::deserialize<e::scene_description>(
      bytes(scene("[" + entry(custom_description::type_guid, R"({"enabled":null})") + "]")));
  if (invalid || invalid.error().path != "/objects/0/components/0/$content/enabled")
    return 7;
  auto null_component = e::meta::deserialize<e::scene_description>(bytes(scene("[null]")));
  if (null_component || null_component.error().path != "/objects/0/components")
    return 33;
  auto null_after_valid =
      e::meta::deserialize<e::scene_description>(bytes(scene("[" + custom + ",null]")));
  if (null_after_valid || null_after_valid.error().path != "/objects/0/components")
    return 34;
  auto extra_envelope = custom;
  extra_envelope.insert(1, "\"extra\":true,");
  if (e::meta::deserialize<e::scene_description>(bytes(scene("[" + extra_envelope + "]"))))
    return 35;
  if (e::meta::deserialize<e::scene_description>(bytes(scene("{}"))))
    return 8;
  auto owner = e::create_native();
  if (!owner)
    return 9;
  auto unsupported = e::create_scene_object(**owner, parsed->objects.front(), {}, {});
  if (unsupported || unsupported.error() != e::error::unsupported)
    return 10;
  // 未发布的旧扁平 Camera 格式不再接受。
  auto old_camera = e::meta::deserialize<e::scene_description>(bytes(scene(
      "[" +
      entry(
          e::camera_description::type_guid,
          R"({"vertical_field_of_view_radians":1,"near_plane":0.1,"far_plane":100,"is_primary":false})") +
      "]")));
  if (old_camera || old_camera.error().path != "/objects/0/components/0/$content/settings")
    return 21;
  auto bad_camera = e::meta::deserialize<e::scene_description>(
      bytes(scene("[" +
                  entry(e::camera_description::type_guid,
                        R"({"settings":{"fov":1,"near":"bad","far":100},"is_primary":false})") +
                  "]")));
  if (bad_camera || bad_camera.error().path != "/objects/0/components/0/$content/settings/near")
    return 22;
  const auto camera =
      entry(e::camera_description::type_guid,
            R"({"settings": {"fov": 1, "near": 0.1, "far": 100},"is_primary":false})");
  auto supported = e::meta::deserialize<e::scene_description>(bytes(scene("[" + camera + "]")));
  if (!supported)
    return 11;
  const auto* description = supported->objects.front().components.get<e::camera_description>();
  if (!description || description->settings.fov != 1.0F || description->settings.near != 0.1F ||
      description->settings.far != 100.0F)
    return 17;
  supported->objects.front().components.update<e::camera_description>([](auto& value) {
    value.settings = {0.8F, 0.2F, 250.0F};
    value.is_primary = true;
  });
  auto camera_bytes = e::meta::serialize(*supported);
  if (!camera_bytes)
    return 18;
  auto camera_roundtrip = e::meta::deserialize<e::scene_description>(*camera_bytes);
  if (!camera_roundtrip)
    return 19;
  const auto* restored = camera_roundtrip->objects.front().components.get<e::camera_description>();
  if (!restored || restored->settings.fov != 0.8F || restored->settings.near != 0.2F ||
      restored->settings.far != 250.0F || !restored->is_primary)
    return 20;
  auto node = e::create_scene_object(**owner, supported->objects.front(), {}, {});
  if (!node)
    return 12;
  (void)(*owner)->object_destroy(*node);
  // Prefab 共用完全相同的组件协议。
  auto prefab_text = scene("[" + camera + "," + custom + "]");
  prefab_text.replace(prefab_text.find("gneiss.scene"), 12, "gneiss.prefab");
  prefab_text.replace(prefab_text.find("scene_uuid"), 10, "prefab_uuid");
  prefab_text.replace(prefab_text.find("\"version\":5"), 11, "\"version\":1");
  prefab_text.insert(1, R"("root_uuid":"00000000-0000-4000-8000-000000000002",)");
  auto prefab = e::meta::deserialize<e::prefab_description>(bytes(prefab_text));
  if (!prefab || prefab->objects.front().components.size() != 2)
    return 13;
  const auto prefab_decodes = decode_calls;
  auto prefab_bytes = e::meta::serialize(*prefab);
  if (decode_calls != prefab_decodes)
    return 16;
  if (!prefab_bytes || !e::meta::deserialize<e::prefab_description>(*prefab_bytes))
    return 14;
  return 0;
} catch (...) {
  return 99;
}
