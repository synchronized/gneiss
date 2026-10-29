// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"camera":{"vertical_field_of_view_radians":1.0,"near_plane":0.1,"far_plane":100,"is_primary":true}}},{"uuid":"00000000-0000-4000-8000-000000000003","parent":"00000000-0000-4000-8000-000000000002","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}}],"unknown":true})";

constexpr std::string_view prefab_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{}},{"name":"Lamp","parent":"00000000-0000-4000-8000-000000000002","prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_guid":"69644f20b2d24e488c7491f4f952ec2d","property_id":1,"value":{"kind":"vec3","value":[4,5,6]}}]},"transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[2,2,2]},"unknown":true,"uuid":"00000000-0000-4000-8000-000000000010"}]})";

[[nodiscard]] std::string replace_once(std::string text, std::string_view source,
                                       std::string_view replacement) {
  const auto position = text.find(source);
  if (position != std::string::npos) {
    text.replace(position, source.size(), replacement);
  }
  return text;
}

[[nodiscard]] bool fails_with(std::string_view json, gneiss::engine::error expected,
                              std::string_view path) {
  gneiss::engine::scene_description scene;
  gneiss::engine::scene_diagnostic diagnostic;
  return gneiss::test_support::assign_decoded(gneiss::engine::parse_scene_description(json), scene,
                                              diagnostic) == expected &&
         diagnostic.error == expected && diagnostic.path == path && scene.objects.empty();
}

class memory_file_system final : public gneiss::test_support::memory_file_fixture {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss::engine::error
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "main.scene.json") {
      return gneiss::engine::error::not_found;
    }
    try {
      out_bytes.resize(content_.size());
      for (std::size_t index = 0U; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return gneiss::engine::error::success;
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
  }

  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& output) const noexcept override {
    if (content_.size() > limit)
      return gneiss::engine::error::invalid_argument;
    return read(path, output);
  }

private:
  std::string content_;
};

} // namespace

int main() try {
  // 反射 codec 保留未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss::engine;
    gneiss::engine::meta::registry_ref registry;
    if (gneiss::test_support::assign_value(e::builtin_resource_registry(), registry).failed())
      return 90;
    auto text = std::string(valid_scene);
    text.insert(1, R"("codec_unknown":{"keep":7},)");
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    e::scene_description value;
    gneiss::engine::meta::serialization_diagnostic detail;
    std::vector<std::byte> encoded;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(value)>>(registry,
                                                                                    source),
            value, detail)
            .failed() ||
        gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, value),
                                             encoded, detail)
            .failed())
      return 91;
    e::scene_description restored;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                       encoded),
            restored, detail)
            .failed() ||
        restored.objects.size() != value.objects.size() ||
        restored.author_json.find("codec_unknown") == std::string::npos)
      return 92;
    const auto saved = restored.author_json;
    const std::string broken = "{";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(
                registry, std::as_bytes(std::span{broken.data(), broken.size()})),
            restored, detail)
            .ok() ||
        restored.author_json != saved)
      return 93;
    std::vector<std::byte> oversized(e::scene_description::maximum_source_bytes + 1U);
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                       oversized),
            restored, detail) != e::error::invalid_argument ||
        restored.author_json != saved)
      return 94;
  }

  gneiss::engine::scene_description scene;
  gneiss::engine::scene_diagnostic diagnostic;
  std::string serialized;
  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_scene_description(valid_scene),
                                           scene, diagnostic) != gneiss::engine::error::success ||
      scene.source_schema_version != 5U || scene.objects.size() != 2U || !scene.objects[0].camera ||
      !scene.objects[1].mesh_renderer ||
      gneiss::test_support::assign_value(gneiss::engine::serialize_scene_description(scene),
                                         serialized) != gneiss::engine::error::success ||
      serialized.find(R"("unknown":true)") == std::string::npos) {
    return 1;
  }

  const auto legacy_array =
      replace_once(std::string(valid_scene), "{", R"({"prefab_instances":[],)");
  if (!fails_with(legacy_array, gneiss::engine::error::invalid_argument, "/prefab_instances"))
    return 95;

  const auto future = replace_once(std::string(valid_scene), "\"version\":5", "\"version\":6");
  const auto old = replace_once(std::string(valid_scene), "\"version\":5", "\"version\":4");
  const auto duplicate =
      replace_once(std::string(valid_scene), "00000000-0000-4000-8000-000000000003",
                   "00000000-0000-4000-8000-000000000002");
  const auto missing_parent =
      replace_once(std::string(valid_scene), R"("parent":"00000000-0000-4000-8000-000000000002")",
                   R"("parent":"00000000-0000-4000-8000-000000000099")");
  const auto cycle = replace_once(std::string(valid_scene), R"("parent":null)",
                                  R"("parent":"00000000-0000-4000-8000-000000000003")");
  const auto invalid_uri = replace_once(std::string(valid_scene), "asset://models/triangle.mesh",
                                        "asset://models/../triangle.mesh");
  const auto invalid_scale =
      replace_once(std::string(valid_scene), R"("scale":[1,1,1])", R"("scale":[1,0,1])");
  const auto invalid_rotation =
      replace_once(std::string(valid_scene), R"("rotation":[0,0,0,1])", R"("rotation":[0,0,0,2])");
  if (!fails_with(future, gneiss::engine::error::unsupported, "/version") ||
      !fails_with(old, gneiss::engine::error::invalid_argument, "/version") ||
      !fails_with(duplicate, gneiss::engine::error::invalid_argument, "/objects/1/uuid") ||
      !fails_with(missing_parent, gneiss::engine::error::invalid_argument, "/objects/1/parent") ||
      !fails_with(cycle, gneiss::engine::error::invalid_argument, "/objects/0/parent") ||
      !fails_with(invalid_uri, gneiss::engine::error::invalid_argument,
                  "/objects/1/components/mesh_renderer") ||
      !fails_with(invalid_scale, gneiss::engine::error::invalid_argument, "/objects/0/transform") ||
      !fails_with(invalid_rotation, gneiss::engine::error::invalid_argument,
                  "/objects/0/transform")) {
    return 2;
  }

  if (gneiss::test_support::assign_decoded(gneiss::engine::parse_scene_description(prefab_scene),
                                           scene, diagnostic) != gneiss::engine::error::success ||
      scene.objects.size() != 2U || scene.objects[1].prefab->overrides.size() != 1U ||
      std::get<gneiss::engine::math::vector3>(
          scene.objects[1].prefab->overrides[0].value.payload)[0] != 4.0F ||
      gneiss::test_support::assign_value(gneiss::engine::serialize_scene_description(scene),
                                         serialized) != gneiss::engine::error::success ||
      serialized.find(R"("kind":"vec3")") == std::string::npos ||
      serialized.find(R"("unknown":true)") == std::string::npos) {
    return 3;
  }

  const auto duplicate_override = replace_once(
      std::string(prefab_scene), R"("overrides":[{)",
      R"("overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_guid":"69644f20b2d24e488c7491f4f952ec2d","property_id":1,"value":{"kind":"vec3","value":[7,8,9]}},{)");
  const auto bad_type = replace_once(std::string(prefab_scene), "69644f20b2d24e488c7491f4f952ec2d",
                                     "69644F20b2d24e488c7491f4f952ec2d");
  const auto bad_value =
      replace_once(std::string(prefab_scene), R"("value":[4,5,6])", R"("value":[4,5])");
  if (!fails_with(duplicate_override, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides") ||
      !fails_with(bad_type, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/type_guid") ||
      !fails_with(bad_value, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/value")) {
    return 4;
  }

  constexpr std::array<std::string_view, 8> other_values{
      R"("kind":"bool","value":true)",
      R"("kind":"int64","value":-5)",
      R"("kind":"uint64","value":5)",
      R"("kind":"float32","value":1.25)",
      R"("kind":"float64","value":2.5)",
      R"("kind":"string","value":"text")",
      R"("kind":"type_id","value":"69644f20b2d24e488c7491f4f952ec2d")",
      R"("kind":"quaternion","value":[0.1,0.2,0.3,0.9])",
  };
  for (const auto value : other_values) {
    const auto candidate =
        replace_once(std::string(prefab_scene), R"("kind":"vec3","value":[4,5,6])", value);
    if (gneiss::test_support::assign_decoded(gneiss::engine::parse_scene_description(candidate),
                                             scene, diagnostic) != gneiss::engine::error::success ||
        gneiss::test_support::assign_value(gneiss::engine::serialize_scene_description(scene),
                                           serialized) != gneiss::engine::error::success) {
      return 5;
    }
    gneiss::engine::scene_description roundtrip;
    if (gneiss::test_support::assign_decoded(gneiss::engine::parse_scene_description(serialized),
                                             roundtrip, diagnostic)
            .failed() ||
        roundtrip.objects[1].prefab->overrides[0].value !=
            scene.objects[1].prefab->overrides[0].value)
      return 5;
    if (value.find("quaternion") != std::string_view::npos) {
      const auto& q = std::get<gneiss::engine::math::quaternion>(
          roundtrip.objects[1].prefab->overrides[0].value.payload);
      if (q.x != 0.1F || q.y != 0.2F || q.z != 0.3F || q.w != 0.9F)
        return 5;
    }
  }

  gneiss::test_support::fixture_files file_system;
  if (file_system.mount("asset://scenes/", std::make_shared<memory_file_system>(std::string(
                                               valid_scene))) != gneiss::engine::error::success)
    return 6;
  gneiss::engine::asset_manager assets(gneiss::test_support::test_files(file_system));
  gneiss::engine::asset_ref<const gneiss::engine::scene_description> loaded;
  if (gneiss::test_support::load_asset(assets, "asset://scenes/main.scene.json", loaded).failed() ||
      !loaded)
    return 6;
  return fails_with("{\"format\":", gneiss::engine::error::invalid_argument, "") ? 0 : 7;
} catch (...) {
  return 99;
}
