// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0101","$content":{"settings": {"fov": 1.0, "near": 0.1, "far": 100},"is_primary":true}}]},{"uuid":"00000000-0000-4000-8000-000000000003","parent":"00000000-0000-4000-8000-000000000002","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}]}],"unknown":true})";

constexpr std::string_view prefab_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]},{"name":"Lamp","parent":"00000000-0000-4000-8000-000000000002","prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_guid":"69644f20b2d24e488c7491f4f952ec2d","property_id":1,"value":{"kind":"vec3","value":[4,5,6]}}]},"transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[2,2,2]},"unknown":true,"uuid":"00000000-0000-4000-8000-000000000010"}]})";

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
  const auto status = gneiss::test_support::assign_decoded(
      gneiss::engine::parse_scene_description(json), scene, diagnostic);
  const bool matched = status == expected && diagnostic.error == expected &&
                       diagnostic.path == path && scene.objects.empty();
  if (!matched)
    std::fprintf(stderr, "expected path %.*s, actual %s\n", static_cast<int>(path.size()),
                 path.data(), diagnostic.path.c_str());
  return matched;
}

bool save_validation_contract() {
  namespace e = gneiss::engine;
  auto parsed = e::parse_scene_description(valid_scene);
  auto prefab = e::parse_scene_description(prefab_scene);
  if (!parsed || !prefab)
    return false;
  const auto rejects = [&](auto change) {
    auto candidate = *parsed;
    change(candidate);
    auto text = e::serialize_scene_description(candidate);
    auto bytes = e::encode_scene(candidate);
    return !text && text.error() == e::error::invalid_argument && !bytes &&
           bytes.error().error == e::error::invalid_argument;
  };
  if (!rejects([](auto& scene) { scene.objects[0].parent_uuid = scene.objects[1].uuid; }) ||
      !rejects([](auto& scene) { scene.objects[1].uuid = scene.objects[0].uuid; }) ||
      !rejects([](auto& scene) {
        scene.objects[0].transform.translation.x = std::numeric_limits<float>::infinity();
      }) ||
      !rejects([](auto& scene) { scene.objects[0].transform.rotation.w = 2.0F; }) ||
      !rejects([](auto& scene) { scene.objects[0].transform.scale.y = 0.0F; }) ||
      !rejects([](auto& scene) {
        scene.objects[0].components.template update<e::camera_description>(
            [](auto& camera) { camera.settings.near = camera.settings.far; });
      }) ||
      !rejects([](auto& scene) {
        scene.objects[1].components.template update<e::mesh_renderer_description>(
            [](auto& mesh) { mesh.mesh_uri = "bad"; });
      }))
    return false;
  const auto rejects_override = [&](auto change) {
    auto candidate = *prefab;
    change(*candidate.objects[1].prefab);
    auto encoded = e::encode_scene(candidate);
    return !encoded && encoded.error().error == e::error::invalid_argument;
  };
  if (!rejects_override([](auto& reference) { reference.uri = "bad"; }) ||
      !rejects_override(
          [](auto& reference) { reference.overrides.push_back(reference.overrides.front()); }) ||
      !rejects_override([](auto& reference) { reference.overrides[0].key.type_guid = {}; }) ||
      !rejects_override([](auto& reference) { reference.overrides[0].key.property_id = {}; }) ||
      !rejects_override([](auto& reference) {
        reference.overrides[0].value.payload = std::numeric_limits<double>::quiet_NaN();
      }) ||
      !rejects_override(
          [](auto& reference) { reference.overrides[0].value.payload = std::monostate{}; }))
    return false;
  // Scene 仍允许多根，不因 Prefab 的唯一根约束变成只能保存一棵树。
  parsed->objects[1].parent_uuid.reset();
  auto forest = e::encode_scene(*parsed);
  return forest && static_cast<bool>(e::decode_scene(*forest));
}

bool guid_identity_contract() {
  namespace e = gneiss::engine;
  auto parsed = e::parse_scene_description(valid_scene);
  if (!parsed || parsed->objects[0].parent_uuid ||
      parsed->objects[1].parent_uuid != parsed->objects[0].uuid)
    return false;
  const auto original = *parsed;
  // 内存身份修改必须反映到文件；不沿用 author_json 中的旧场景 UUID。
  parsed->uuid = *e::guid::parse("abcdef01-2345-4678-9abc-def012345678");
  auto encoded = e::serialize_scene_description(*parsed);
  if (!encoded)
    return false;
  auto restored = e::parse_scene_description(*encoded);
  if (!restored || restored->uuid != parsed->uuid ||
      restored->objects[1].parent_uuid != parsed->objects[0].uuid)
    return false;
  parsed->objects[0].parent_uuid = e::guid{};
  if (e::serialize_scene_description(*parsed))
    return false;
  *parsed = original;
  parsed->objects[0].uuid = {};
  if (e::serialize_scene_description(*parsed))
    return false;
  *parsed = original;
  parsed->uuid = {};
  if (e::serialize_scene_description(*parsed))
    return false;
  for (const auto malformed :
       {"00000000-0000-0000-0000-000000000000", "ABCDEF01-2345-4678-9ABC-DEF012345678",
        "abcdef01234546789abcdef012345678", "invalid"}) {
    const auto text =
        replace_once(std::string(valid_scene), "00000000-0000-4000-8000-000000000002", malformed);
    if (!fails_with(text, e::error::invalid_argument, "/objects/0/uuid"))
      return false;
  }
  const auto nil_parent =
      replace_once(std::string(valid_scene), R"("parent":"00000000-0000-4000-8000-000000000002")",
                   R"("parent":"00000000-0000-0000-0000-000000000000")");
  if (!fails_with(nil_parent, e::error::invalid_argument, "/objects/1/parent"))
    return false;
  const auto nil_source =
      replace_once(std::string(prefab_scene), "10000000-0000-4000-8000-000000000002",
                   "00000000-0000-0000-0000-000000000000");
  if (e::parse_scene_description(nil_source))
    return false;
  const auto low = *e::guid::parse("00ffffff-ffff-4fff-8fff-ffffffffffff");
  const auto high = *e::guid::parse("01000000-0000-4000-8000-000000000000");
  return low < high && low.string() < high.string() &&
         e::guid_hash{}(low) == e::guid_hash{}(*e::guid::parse(low.string()));
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
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  if (!save_validation_contract())
    return 11;
  if (!guid_identity_contract())
    return 98;
  {
    auto fresh = gneiss::engine::parse_scene_description(valid_scene);
    if (!fresh)
      return 40;
    fresh->author_json.clear();
    auto bytes = gneiss::engine::encode_scene(*fresh);
    if (!bytes)
      return 41;
    auto restored = gneiss::engine::decode_scene(*bytes);
    if (!restored || restored->uuid != fresh->uuid ||
        restored->objects.size() != fresh->objects.size())
      return 42;
  }
  // 反射 codec 保留未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss::engine;
    gneiss::engine::meta::registry_ref registry;
    if (gneiss::test_support::assign_value(e::meta::default_registry(), registry).failed())
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
      scene.source_schema_version != 5U || scene.objects.size() != 2U ||
      !scene.objects[0].components.template get<gneiss::engine::camera_description>() ||
      !scene.objects[1].components.template get<gneiss::engine::mesh_renderer_description>() ||
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
  // 单位变换默认值不允许掩盖文件中缺失的必填分量。
  for (const auto field :
       {R"("translation":[0,1,2],)", R"("rotation":[0,0,0,1],)", R"(,"scale":[1,1,1])"}) {
    auto missing = replace_once(std::string(valid_scene), field, "");
    auto parsed = gneiss::engine::parse_scene_description(missing);
    if (parsed || parsed.error().error != gneiss::engine::error::invalid_argument ||
        !parsed.error().path.starts_with("/objects/0/transform/"))
      return 96;
  }
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
                  "/objects/1/components/0/$content") ||
      !fails_with(invalid_scale, gneiss::engine::error::invalid_argument,
                  "/objects/0/transform/scale") ||
      !fails_with(invalid_rotation, gneiss::engine::error::invalid_argument,
                  "/objects/0/transform/rotation")) {
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
