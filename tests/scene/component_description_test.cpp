// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/core/serialization/serialization.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_file.hpp"
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
e::result<void*, e::serialization::diagnostic> encode(const e::serialization::write_context&,
                                                      e::serialization::writer& output,
                                                      const custom_description& value) {
  auto root = output.object();
  auto uuid = output.scalar(e::value{value.uuid});
  if (!uuid || !output.member(root, "uuid", *uuid))
    return e::serialization::diagnostic{.error = e::error::out_of_memory};
  if (value.enabled) {
    auto flag = output.scalar(e::value{true});
    if (!flag || !output.member(root, "enabled", *flag))
      return e::serialization::diagnostic{.error = e::error::out_of_memory};
  }
  return root;
}
e::result<custom_description, e::serialization::diagnostic>
decode(const e::serialization::read_context&, e::serialization::reader& input, const void* node) {
  ++decode_calls;
  auto uuid = input.scalar(input.member(node, "uuid"), e::value_kind::guid);
  if (!uuid)
    return e::serialization::diagnostic{.error = e::error::invalid_argument};
  custom_description result;
  result.uuid = std::get<e::guid>(uuid->payload);
  if (input.is_object(node) && input.size(node) == 1)
    return result;
  auto flag = input.scalar(input.member(node, "enabled"), e::value_kind::boolean);
  if (input.is_object(node) && input.size(node) == 2 && flag && std::get<bool>(flag->payload)) {
    result.enabled = true;
    return result;
  }
  return e::serialization::diagnostic{
      .path = "/enabled", .message = "缺少布尔字段", .error = e::error::invalid_argument};
}
std::string scene(std::string_view components) {
  return std::string(
             R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":)") +
         std::string(components) + "}]}";
}
std::string entry(e::guid type, std::string_view content) {
  auto fields = std::string(content);
  fields.insert(1, "\"uuid\":\"" + e::guid::generate().string() + "\"" +
                       (fields.size() > 2 ? "," : ""));
  return std::string(R"({"$type_guid":")") + type.string() + R"(","$content":)" + fields + "}";
}
int main() try {
  if (e::initialize_meta([]() {
        auto status = e::meta::registration::type<reflected_description>()
                          .base<e::component_description_base>()
                          .property<&reflected_description::speed>(e::meta::property_id{1}, "speed")
                          .commit();
        if (status.failed())
          return status;
        return e::meta::registration::type<custom_description>()
            .base<e::component_description_base>()
            .extend(gneiss::engine::serialization::codec<&encode, &decode>)
            .commit();
      }).failed())
    return 1;
  // 新格式要求组件自身保存 UUID，不保留旧外壳或缺失身份的兼容读取。
  const auto missing_identity = std::string(R"([{"$type_guid":")") +
                                reflected_description::type_guid.string() +
                                R"(","$content":{"speed":3}}])";
  const auto old_envelope = std::string(R"([{"uuid":")") + e::guid::generate().string() +
                            R"(","description":)" +
                            entry(reflected_description::type_guid, R"({"speed":3})") + "}]";
  if (e::serialization::json::decode<e::component_descriptions>(missing_identity) ||
      e::serialization::json::decode<e::component_descriptions>(old_envelope))
    return 61;
  // 项目组件只注册字段，不写 codec；Scene/Prefab 的 GUID 分派共用默认表。
  const auto reflected = entry(reflected_description::type_guid, R"({"speed":3})");
  auto reflected_scene = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + reflected + "]")));
  if (!reflected_scene ||
      reflected_scene->objects.front().components.get<reflected_description>()->speed != 3)
    return 30;
  e::prefab_description reflected_prefab;
  reflected_prefab.uuid = reflected_scene->uuid;
  reflected_prefab.objects = reflected_scene->objects;
  reflected_prefab.root_uuid = reflected_prefab.objects.front().uuid;
  auto reflected_prefab_bytes = gneiss::engine::asset_file::serialize_bytes(reflected_prefab);
  if (!reflected_prefab_bytes)
    return 31;
  auto restored_prefab =
      gneiss::engine::asset_file::deserialize<e::prefab_description>(*reflected_prefab_bytes);
  if (!restored_prefab ||
      restored_prefab->objects.front().components.get<reflected_description>()->speed != 3)
    return 32;
  const auto custom = entry(custom_description::type_guid, R"({"enabled":true})");
  auto parsed = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + custom + "]")));
  if (!parsed || parsed->objects.front().components.size() != 1 ||
      !parsed->objects.front().components.get<custom_description>()->enabled ||
      parsed->objects.front().components.get<e::camera_description>())
    return 2;
  // 实例身份不等于类型；替换保留身份，删后重建获得新身份，文件往返保留身份。
  const auto identity = parsed->objects.front().components.identity(custom_description::type_guid);
  if (!identity || identity == custom_description::type_guid)
    return 50;
  auto identities = parsed->objects.front().components;
  identities.set(custom_description{false});
  if (identities.identity(custom_description::type_guid) != identity)
    return 51;
  auto encoded_identities = e::serialization::json::encode(identities);
  if (!encoded_identities)
    return 52;
  auto restored_identities =
      e::serialization::json::decode<e::component_descriptions>(*encoded_identities);
  if (!restored_identities ||
      restored_identities->identity(custom_description::type_guid) != identity)
    return 53;
  identities.erase<custom_description>();
  identities.set(custom_description{});
  if (identities.identity(custom_description::type_guid) == identity)
    return 54;
  // 改变身份或替换内容都不能修改外部仍持有的描述。
  auto source = e::make_component_description(reflected_description{});
  const auto source_uuid = source->uuid;
  auto rebound = e::with_component_identity(source, e::guid::generate());
  if (!rebound || source->uuid != source_uuid || (*rebound)->uuid == source_uuid ||
      rebound->get() == source.get())
    return 57;
  e::component_descriptions shared_values{source};
  auto replacement = e::make_component_description(reflected_description{});
  const auto replacement_uuid = replacement->uuid;
  shared_values.set(replacement);
  if (shared_values.identity(reflected_description::type_guid) != source_uuid ||
      replacement->uuid != replacement_uuid || source->uuid != source_uuid)
    return 58;
  auto stored = e::serialization::json::encode(shared_values);
  if (!stored || stored->find("\"description\"") != std::string::npos ||
      stored->find(source_uuid.string()) == std::string::npos)
    return 59;
  auto duplicate_entries = identities.entries();
  reflected_description duplicate_identity;
  duplicate_identity.uuid = duplicate_entries.front()->uuid;
  bool rejected_identity = false;
  try {
    identities.set(duplicate_identity);
  } catch (const std::invalid_argument&) {
    rejected_identity = true;
  }
  if (!rejected_identity || identities.size() != 1)
    return 60;
  duplicate_entries.push_back(e::make_component_description(duplicate_identity));
  if (identities.replace(duplicate_entries) != e::error::invalid_argument || identities.size() != 1)
    return 55;
  duplicate_identity.uuid = {};
  duplicate_entries = {e::make_component_description(duplicate_identity)};
  if (identities.replace(duplicate_entries) != e::error::invalid_argument)
    return 56;
  // 组件属性只能整项替换；失败不修改已有组件或共享快照。
  auto guarded = parsed->objects.front();
  auto object_view = e::meta::view(guarded);
  if (!object_view)
    return 37;
  auto property = object_view->member("components");
  if (!property || !property->as_sequence() || !property->as_sequence()->supports_replacement() ||
      property->as_sequence()->at(0).error() != e::error::unsupported ||
      property->as_sequence()->resize(2) != e::error::unsupported ||
      !property->as_const().as_sequence())
    return 38;
  auto collection_bytes = gneiss::engine::serialization::json::encode_bytes(guarded.components);
  if (!collection_bytes ||
      !gneiss::engine::serialization::json::decode<e::component_descriptions>(*collection_bytes))
    return 41;
  auto original = *guarded.components.begin();
  auto null_status = gneiss::engine::serialization::json::read_into(*property, bytes("[null]"));
  auto duplicate_status = gneiss::engine::serialization::json::read_into(
      *property, bytes("[" + custom + "," + custom + "]"));
  if (null_status.has_value() || duplicate_status.has_value() || guarded.components.size() != 1 ||
      *guarded.components.begin() != original)
    return 39;
  if (gneiss::engine::serialization::json::read_into(*property, bytes("[]")).failed() ||
      !guarded.components.empty() || parsed->objects.front().components.empty())
    return 40;
  auto copy = *parsed;
  auto incomplete = e::serialization::restore_value<custom_description>(
      e::value{e::record_value{e::record_value_data{custom_description::type_guid, {}}}});
  if (incomplete || incomplete.error().error != e::error::unsupported)
    return 62;
  bool invoked = false;
  if (copy.objects.front().components.update<custom_description>([&](auto& value) {
        invoked = true;
        value.enabled = false;
      }) != e::error::unsupported ||
      invoked)
    return 42;
  copy.objects.front().components.set(custom_description{false});
  if (!parsed->objects.front().components.get<custom_description>()->enabled)
    return 3;
  // false 编码省略 enabled，保存不能把原始 true 字段合并回来。
  const auto scene_decodes = decode_calls;
  auto encoded = gneiss::engine::asset_file::serialize_bytes(copy);
  // 保存只调用编码器，不能通过反向解码产生新的组件对象或副作用。
  if (decode_calls != scene_decodes)
    return 15;
  auto roundtrip =
      encoded ? gneiss::engine::asset_file::deserialize<e::scene_description>(*encoded)
              : e::result<e::scene_description, e::serialization::diagnostic>{encoded.error()};
  if (!roundtrip || roundtrip->objects.front().components.get<custom_description>()->enabled)
    return 4;
  auto unknown = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + entry(e::guid::from_bytes({99}), "{}") + "]")));
  if (unknown || unknown.error().error != e::error::not_found ||
      unknown.error().path.find("/objects/0/components/0") != 0)
    return 5;
  auto duplicate = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + custom + "," + custom + "]")));
  if (duplicate || duplicate.error().error != e::error::invalid_argument)
    return 6;
  auto invalid = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + entry(custom_description::type_guid, R"({"enabled":null})") + "]")));
  if (invalid || invalid.error().path != "/objects/0/components/0/$content/enabled")
    return 7;
  auto null_component =
      gneiss::engine::asset_file::deserialize<e::scene_description>(bytes(scene("[null]")));
  if (null_component || null_component.error().path != "/objects/0/components")
    return 33;
  auto null_after_valid = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + custom + ",null]")));
  if (null_after_valid || null_after_valid.error().path != "/objects/0/components")
    return 34;
  auto extra_envelope = custom;
  extra_envelope.insert(extra_envelope.find("\"$type_guid\""), "\"extra\":true,");
  if (gneiss::engine::asset_file::deserialize<e::scene_description>(
          bytes(scene("[" + extra_envelope + "]"))))
    return 35;
  if (gneiss::engine::asset_file::deserialize<e::scene_description>(bytes(scene("{}"))))
    return 8;
  auto owner = e::create_native();
  if (!owner)
    return 9;
  auto destination = (*owner)->create_scene();
  if (!destination)
    return 9;
  const auto create =
      [&](const e::scene_description& description) -> e::result<e::scene_instantiation_result> {
    auto composed = e::compose_scene(description, {});
    if (!composed)
      return composed.error();
    return e::instantiate(*destination, *composed, {});
  };
  auto unsupported = create(*parsed);
  if (unsupported || unsupported.error() != e::error::unsupported)
    return 10;
  // 未发布的旧扁平 Camera 格式不再接受。
  auto old_camera = gneiss::engine::serialization::json::decode<e::scene_description>(bytes(scene(
      "[" +
      entry(
          e::camera_description::type_guid,
          R"({"vertical_field_of_view_radians":1,"near_plane":0.1,"far_plane":100,"is_primary":false})") +
      "]")));
  if (old_camera || old_camera.error().path != "/objects/0/components/0/$content/settings")
    return 21;
  auto bad_camera = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" +
                  entry(e::camera_description::type_guid,
                        R"({"settings":{"fov":1,"near":"bad","far":100},"is_primary":false})") +
                  "]")));
  if (bad_camera || bad_camera.error().path != "/objects/0/components/0/$content/settings/near")
    return 22;
  const auto camera =
      entry(e::camera_description::type_guid,
            R"({"settings": {"fov": 1, "near": 0.1, "far": 100},"is_primary":false})");
  auto supported = gneiss::engine::asset_file::deserialize<e::scene_description>(
      bytes(scene("[" + camera + "]")));
  if (!supported)
    return 11;
  const auto* description = supported->objects.front().components.get<e::camera_description>();
  if (!description || description->settings.fov != 1.0F || description->settings.near != 0.1F ||
      description->settings.far != 100.0F)
    return 17;
  if (supported->objects.front()
          .components
          .update<e::camera_description>([](auto& value) {
            value.settings = {0.8F, 0.2F, 250.0F};
            value.is_primary = true;
          })
          .failed())
    return 43;
  // 通用捕获允许结构完整的非法值；组件编辑必须在提交前执行领域校验。
  auto& components = supported->objects.front().components;
  const auto retained_camera = *components.find(e::camera_description::type_guid);
  auto rejected = components.update<e::camera_description>(
      [](auto& value) { value.settings.near = value.settings.far + 1.0F; });
  if (rejected != e::error::invalid_argument ||
      *components.find(e::camera_description::type_guid) != retained_camera ||
      components.get<e::camera_description>()->settings.near != 0.2F)
    return 63;
  auto rejected_camera_identity =
      components.update<e::camera_description>([](auto& value) { value.uuid = {}; });
  if (rejected_camera_identity != e::error::invalid_argument ||
      *components.find(e::camera_description::type_guid) != retained_camera)
    return 64;
  auto camera_bytes = gneiss::engine::asset_file::serialize_bytes(*supported);
  if (!camera_bytes)
    return 18;
  auto camera_roundtrip =
      gneiss::engine::asset_file::deserialize<e::scene_description>(*camera_bytes);
  if (!camera_roundtrip)
    return 19;
  const auto* restored = camera_roundtrip->objects.front().components.get<e::camera_description>();
  if (!restored || restored->settings.fov != 0.8F || restored->settings.near != 0.2F ||
      restored->settings.far != 250.0F || !restored->is_primary)
    return 20;
  auto node = create(*supported);
  if (!node)
    return 12;
  if (destination->unload().failed())
    return 12;
  // Prefab 共用完全相同的组件协议。
  auto prefab_text = scene("[" + camera + "," + custom + "]");
  prefab_text.replace(prefab_text.find("gneiss.scene"), 12, "gneiss.prefab");
  prefab_text.replace(prefab_text.find("scene_uuid"), 10, "prefab_uuid");
  prefab_text.replace(prefab_text.find("\"version\":5"), 11, "\"version\":1");
  prefab_text.insert(1, R"("root_uuid":"00000000-0000-4000-8000-000000000002",)");
  auto prefab = gneiss::engine::asset_file::deserialize<e::prefab_description>(bytes(prefab_text));
  if (!prefab || prefab->objects.front().components.size() != 2)
    return 13;
  const auto prefab_decodes = decode_calls;
  auto prefab_bytes = gneiss::engine::asset_file::serialize_bytes(*prefab);
  if (decode_calls != prefab_decodes)
    return 16;
  if (!prefab_bytes ||
      !gneiss::engine::asset_file::deserialize<e::prefab_description>(*prefab_bytes))
    return 14;
  return 0;
} catch (...) {
  return 99;
}
