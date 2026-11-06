// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
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

struct format_probe {
  static constexpr std::string_view type_name = "format_probe";
  static constexpr gneiss::engine::guid type_guid = gneiss::engine::guid::from_bytes({98, 11});
  std::string text;
};
constexpr gneiss::engine::asset_file::format probe_format{"probe", 1, 80};
bool format_node_contract() {
  namespace e = gneiss::engine;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry ||
      m::type_builder<format_probe>{*registry}
          .property<&format_probe::text>(m::property_id{1}, "text", m::property_flags::required)
          .serialization(true)
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  e::serialization::context ctx{*registry};
  const auto encode = [&](const format_probe& object) {
    auto view = m::instance::from(*registry, object);
    return e::asset_file::serialize_instance(ctx, probe_format, *view);
  };
  const auto decode = [&](std::span<const std::byte> input) {
    return e::asset_file::deserialize_object(ctx, probe_format, e::runtime_type_of<format_probe>(),
                                             input);
  };
  auto encoded = encode(format_probe{"ok"});
  if (!encoded)
    return false;
  auto decoded = decode(*encoded);
  if (!decoded || static_cast<format_probe*>(decoded->get())->text != "ok")
    return false;
  auto plain = e::serialization::json::serialize(ctx, format_probe{"ok"});
  if (!plain || *plain != R"({"text":"ok"})")
    return false;
  auto huge = encode(format_probe{std::string(100, 'x')});
  if (huge || huge.error().error != e::error::invalid_argument)
    return false;
  for (std::string_view text : {R"({"format":"probe","version":1,"text":"ok","unknown":1})",
                                R"({"format":"probe","format":"probe","version":1,"text":"ok"})",
                                R"({"format":"probe","version":1})"}) {
    auto invalid = decode(std::as_bytes(std::span{text.data(), text.size()}));
    if (invalid || invalid.error().error != e::error::invalid_argument)
      return false;
  }
  return true;
}

constexpr std::string_view valid_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0101","$content":{"settings": {"fov": 1.0, "near": 0.1, "far": 100},"is_primary":true}}]},{"uuid":"00000000-0000-4000-8000-000000000003","parent":"00000000-0000-4000-8000-000000000002","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}]}],"unknown":true})";

constexpr std::string_view prefab_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]},{"name":"Lamp","parent":"00000000-0000-4000-8000-000000000002","prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"key":{"target":{"source_node_uuid":"10000000-0000-4000-8000-000000000002","target_type_guid":"69644f20-b2d2-4e48-8c74-91f4f952ec2d"},"path":[{"$kind":"property","$content":1}]},"value":{"$kind":"value","$content":{"$kind":"vec3","$content":[4,5,6]}}}]},"transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[2,2,2]},"unknown":true,"uuid":"00000000-0000-4000-8000-000000000010"}]})";

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
  gneiss::engine::serialization::diagnostic diagnostic;
  const auto status = gneiss::test_support::assign_decoded(
      gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(json), scene,
      diagnostic);
  const bool matched = status == expected && diagnostic.error == expected &&
                       diagnostic.path == path && scene.objects.empty();
  if (!matched)
    std::fprintf(stderr, "expected path %.*s, actual %s\n", static_cast<int>(path.size()),
                 path.data(), diagnostic.path.c_str());
  return matched;
}

bool save_validation_contract() {
  namespace e = gneiss::engine;
  auto parsed = gneiss::engine::asset_file::deserialize<e::scene_description>(valid_scene);
  auto prefab = gneiss::engine::asset_file::deserialize<e::scene_description>(prefab_scene);
  if (!parsed || !prefab)
    return false;
  const auto rejects = [&](auto change) {
    auto candidate = *parsed;
    change(candidate);
    auto text = gneiss::engine::asset_file::serialize(candidate);
    auto bytes = gneiss::engine::asset_file::serialize_bytes(candidate);
    return !text && text.error().error == e::error::invalid_argument && !bytes &&
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
    auto encoded = gneiss::engine::asset_file::serialize_bytes(candidate);
    return !encoded && encoded.error().error == e::error::invalid_argument;
  };
  if (!rejects_override([](auto& reference) { reference.uri = "bad"; }) ||
      !rejects_override(
          [](auto& reference) { reference.overrides.push_back(reference.overrides.front()); }) ||
      !rejects_override(
          [](auto& reference) { reference.overrides[0].key.target.target_type_guid = {}; }) ||
      !rejects_override([](auto& reference) { reference.overrides[0].key.path.front() = {}; }) ||
      !rejects_override([](auto& reference) {
        std::get<gneiss::engine::value>(reference.overrides[0].value).payload =
            std::numeric_limits<double>::quiet_NaN();
      }) ||
      !rejects_override([](auto& reference) {
        std::get<gneiss::engine::value>(reference.overrides[0].value).payload = std::monostate{};
      }))
    return false;
  for (const e::value& vector :
       {e::value{e::math::vector2{1, 2}}, e::value{e::math::vector4{1, 2, 3, 4}}}) {
    auto candidate = *prefab;
    candidate.objects[1].prefab->overrides[0].value = vector;
    auto encoded = gneiss::engine::asset_file::serialize_bytes(candidate);
    if (!encoded)
      return false;
    auto decoded = gneiss::engine::asset_file::deserialize<e::scene_description>(*encoded);
    if (!decoded ||
        decoded->objects[1].prefab->overrides[0].value != e::prefab_override_value{vector})
      return false;
  }
  if (!rejects_override([](auto& reference) {
        reference.overrides[0].value =
            e::value{e::math::vector2{1, std::numeric_limits<float>::infinity()}};
      }))
    return false;
  // Scene 仍允许多根，不因 Prefab 的唯一根约束变成只能保存一棵树。
  parsed->objects[1].parent_uuid = {};
  auto forest = gneiss::engine::asset_file::serialize_bytes(*parsed);
  return forest &&
         static_cast<bool>(gneiss::engine::asset_file::deserialize<e::scene_description>(*forest));
}

bool guid_identity_contract() {
  namespace e = gneiss::engine;
  auto parsed = gneiss::engine::asset_file::deserialize<e::scene_description>(valid_scene);
  if (!parsed || parsed->objects[0].parent_uuid ||
      parsed->objects[1].parent_uuid != parsed->objects[0].uuid)
    return false;
  const auto original = *parsed;
  // 内存身份修改必须反映到文件；不沿用 输入文件中的旧场景 UUID。
  parsed->uuid = *e::guid::parse("abcdef01-2345-4678-9abc-def012345678");
  auto encoded = gneiss::engine::asset_file::serialize(*parsed);
  if (!encoded)
    return false;
  auto restored = gneiss::engine::asset_file::deserialize<e::scene_description>(*encoded);
  if (!restored || restored->uuid != parsed->uuid ||
      restored->objects[1].parent_uuid != parsed->objects[0].uuid)
    return false;
  parsed->objects[0].parent_uuid = e::guid{};
  if (!gneiss::engine::asset_file::serialize(*parsed))
    return false;
  *parsed = original;
  parsed->objects[0].uuid = {};
  if (gneiss::engine::asset_file::serialize(*parsed))
    return false;
  *parsed = original;
  parsed->uuid = {};
  if (gneiss::engine::asset_file::serialize(*parsed))
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
                   R"("parent":null)");
  if (!gneiss::engine::asset_file::deserialize<e::scene_description>(nil_parent))
    return false;
  const auto null_identity =
      replace_once(std::string(valid_scene), R"("uuid":"00000000-0000-4000-8000-000000000002")",
                   R"("uuid":null)");
  if (!fails_with(null_identity, e::error::invalid_argument, "/objects/0/uuid"))
    return false;
  const auto nil_source =
      replace_once(std::string(prefab_scene), "10000000-0000-4000-8000-000000000002",
                   "00000000-0000-0000-0000-000000000000");
  if (gneiss::engine::asset_file::deserialize<e::scene_description>(nil_source))
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
  if (!format_node_contract())
    return 97;
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  if (!save_validation_contract())
    return 11;
  if (!guid_identity_contract())
    return 98;
  {
    auto fresh =
        gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(valid_scene);
    if (!fresh)
      return 40;
    auto bytes = gneiss::engine::asset_file::serialize_bytes(*fresh);
    if (!bytes)
      return 41;
    auto restored =
        gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(*bytes);
    if (!restored || restored->uuid != fresh->uuid ||
        restored->objects.size() != fresh->objects.size())
      return 42;
  }
  // 反射 codec 忽略未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss::engine;
    auto text = std::string(valid_scene);
    text.insert(1, R"("codec_unknown":{"keep":7},)");
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    e::scene_description value;
    gneiss::engine::serialization::diagnostic detail;
    std::vector<std::byte> encoded;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(value)>>(source),
            value, detail)
            .failed() ||
        gneiss::test_support::assign_decoded(gneiss::engine::asset_file::serialize_bytes(value),
                                             encoded, detail)
            .failed())
      return 91;
    e::scene_description restored;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                encoded),
            restored, detail)
            .failed() ||
        restored.objects.size() != value.objects.size() ||
        (*gneiss::engine::asset_file::serialize(restored)).find("codec_unknown") !=
            std::string::npos)
      return 92;
    const auto saved = (*gneiss::engine::asset_file::serialize(restored));
    const std::string broken = "{";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                std::as_bytes(std::span{broken.data(), broken.size()})),
            restored, detail)
            .ok() ||
        (*gneiss::engine::asset_file::serialize(restored)) != saved)
      return 93;
    std::vector<std::byte> oversized(e::scene_description::maximum_source_bytes + 1U);
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                oversized),
            restored, detail) != e::error::invalid_argument ||
        (*gneiss::engine::asset_file::serialize(restored)) != saved)
      return 94;
  }

  gneiss::engine::scene_description scene;
  gneiss::engine::serialization::diagnostic diagnostic;
  std::string serialized;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(valid_scene),
          scene, diagnostic) != gneiss::engine::error::success ||
      scene.objects.size() != 2U ||
      !scene.objects[0].components.template get<gneiss::engine::camera_description>() ||
      !scene.objects[1].components.template get<gneiss::engine::mesh_renderer_description>() ||
      gneiss::test_support::assign_value(gneiss::engine::asset_file::serialize(scene),
                                         serialized) != gneiss::engine::error::success ||
      serialized.find(R"("unknown":true)") != std::string::npos) {
    return 1;
  }

  const auto legacy_array =
      replace_once(std::string(valid_scene), "{", R"({"prefab_instances":[],)");
  if (!fails_with(legacy_array, gneiss::engine::error::invalid_argument, "/prefab_instances"))
    return 95;

  for (auto version : {"0", "-1", "1.5", "null", "\"5\""}) {
    auto malformed = replace_once(std::string(valid_scene), "\"version\":5",
                                  std::string("\"version\":") + version);
    if (!fails_with(malformed, gneiss::engine::error::invalid_argument, "/version"))
      return 112;
  }
  for (auto header : {R"({"format":"gneiss.scene",)", R"({"version":5,)"}) {
    if (!fails_with(replace_once(std::string(valid_scene), "{", header),
                    gneiss::engine::error::invalid_argument, ""))
      return 113;
  }
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
    auto parsed =
        gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(missing);
    if (parsed || parsed.error().error != gneiss::engine::error::invalid_argument ||
        !parsed.error().path.starts_with("/objects/0/transform/"))
      return 96;
  }
  const auto invalid_scale =
      replace_once(std::string(valid_scene), R"("scale":[1,1,1])", R"("scale":[1,0,1])");
  const auto invalid_rotation =
      replace_once(std::string(valid_scene), R"("rotation":[0,0,0,1])", R"("rotation":[0,0,0,2])");
  if (!fails_with(future, gneiss::engine::error::unsupported, "/version") ||
      !fails_with(old, gneiss::engine::error::unsupported, "/version") ||
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

  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(prefab_scene),
          scene, diagnostic) != gneiss::engine::error::success ||
      scene.objects.size() != 2U || scene.objects[1].prefab->overrides.size() != 1U ||
      std::get<gneiss::engine::math::vector3>(
          std::get<gneiss::engine::value>(scene.objects[1].prefab->overrides[0].value)
              .payload)[0] != 4.0F ||
      gneiss::test_support::assign_value(gneiss::engine::asset_file::serialize(scene),
                                         serialized) != gneiss::engine::error::success ||
      serialized.find(R"("$kind":"vec3")") == std::string::npos ||
      serialized.find(R"("unknown":true)") != std::string::npos) {
    return 3;
  }

  {
    using namespace gneiss::engine;
    auto null_prefab = gneiss::engine::asset_file::deserialize<scene_description>(replace_once(
        std::string(valid_scene), R"("components":[)", R"("prefab":null,"components":[)"));
    if (!null_prefab || null_prefab->objects[0].prefab)
      return 97;
    auto slot = meta::instance::from(scene.objects[1]);
    if (!slot)
      return 98;
    auto reference = slot->property("prefab")->as_optional();
    if (!reference || !*reference->has_value())
      return 98;
    auto contents = reference->get();
    if (!contents || contents->properties()->size() != 3 ||
        *contents->property("overrides")->as_sequence()->size() != 1)
      return 98;
    auto copied = scene.objects[1];
    copied.prefab->uri = "asset://prefabs/copy.prefab.json";
    copied.prefab->overrides.clear();
    if (scene.objects[1].prefab->overrides.empty() ||
        scene.objects[1].prefab->uri == copied.prefab->uri)
      return 99;
    auto bytes = gneiss::engine::serialization::json::serialize_bytes(*scene.objects[1].prefab);
    if (!bytes)
      return 100;
    auto restored =
        gneiss::engine::serialization::json::deserialize<prefab_reference_description>(*bytes);
    if (!restored || restored->overrides.size() != 1 ||
        restored->uri != scene.objects[1].prefab->uri)
      return 100;
    auto conflict = *restored;
    conflict.component_overrides.push_back(
        {.source_node_uuid = conflict.overrides[0].key.target.source_node_uuid,
         .type_guid = conflict.overrides[0].key.target.target_type_guid});
    if (gneiss::engine::serialization::json::serialize_bytes(conflict))
      return 101;
    conflict = *restored;
    conflict.uri = "asset://../bad";
    auto rejected = gneiss::engine::serialization::json::serialize_bytes(conflict);
    if (rejected || rejected.error().path != "/uri")
      return 102;
  }
  if (!fails_with(replace_once(std::string(prefab_scene),
                               R"("uri":"asset://prefabs/lamp.prefab.json")",
                               R"("uri":"asset://../bad")"),
                  gneiss::engine::error::invalid_argument, "/objects/1/prefab/uri"))
    return 103;

  {
    using namespace gneiss::engine;
    // 通用入口与场景入口遵循相同的对象校验；缺失组件默认空列表。
    const std::string object_json =
        R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})";
    auto decode = [](std::string_view text) {
      return gneiss::engine::serialization::json::deserialize<object_description>(
          std::as_bytes(std::span{text.data(), text.size()}));
    };
    auto empty = decode(object_json);
    if (!empty || !empty->components.empty() || empty->prefab)
      return 104;
    auto empty_bytes = gneiss::engine::serialization::json::serialize_bytes(*empty);
    if (!empty_bytes)
      return 105;
    const std::string empty_json(reinterpret_cast<const char*>(empty_bytes->data()),
                                 empty_bytes->size());
    if (empty_json.find(R"("components":[])") == std::string::npos ||
        empty_json.find(R"("prefab":null)") == std::string::npos)
      return 105;
    auto template_bytes = gneiss::engine::serialization::json::serialize_bytes(scene.objects[1]);
    if (!template_bytes)
      return 106;
    const std::string template_json(reinterpret_cast<const char*>(template_bytes->data()),
                                    template_bytes->size());
    if (template_json.find(R"("components":[])") == std::string::npos)
      return 106;
    auto template_copy = decode(template_json);
    if (!template_copy || !template_copy->prefab || !template_copy->components.empty())
      return 106;
    auto invalid_uuid = decode(replace_once(
        object_json, R"("uuid":"00000000-0000-4000-8000-000000000002")", R"("uuid":null)"));
    if (invalid_uuid || invalid_uuid.error().path != "/uuid")
      return 107;
    empty->uuid = {};
    auto invalid_encoded = gneiss::engine::serialization::json::serialize_bytes(*empty);
    if (invalid_encoded || invalid_encoded.error().path != "/uuid")
      return 107;
    auto invalid_components = decode(replace_once(object_json, "{", R"({"components":{},)"));
    if (invalid_components || invalid_components.error().path != "/components")
      return 108;
    auto conflict = scene.objects[1];
    auto normal = gneiss::engine::asset_file::deserialize<scene_description>(valid_scene);
    if (!normal)
      return 109;
    conflict.components = normal->objects[0].components;
    auto rejected = gneiss::engine::serialization::json::serialize_bytes(conflict);
    if (rejected || rejected.error().path != "/components")
      return 109;
    auto component_bytes = gneiss::engine::serialization::json::serialize_bytes(normal->objects[0]);
    if (!component_bytes)
      return 110;
    std::string component_json(reinterpret_cast<const char*>(component_bytes->data()),
                               component_bytes->size());
    component_json =
        replace_once(component_json, R"("prefab":null)",
                     R"("prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[]})");
    auto conflict_decoded = decode(component_json);
    if (conflict_decoded || conflict_decoded.error().path != "/components")
      return 110;
    auto missing = gneiss::engine::asset_file::deserialize<scene_description>(
        replace_once(std::string(prefab_scene), R"(,"components":[])", ""));
    if (!missing || !missing->objects[0].components.empty())
      return 111;
  }

  const auto duplicate_override = replace_once(
      std::string(prefab_scene), R"("overrides":[{)",
      R"("overrides":[{"key":{"target":{"source_node_uuid":"10000000-0000-4000-8000-000000000002","target_type_guid":"69644f20-b2d2-4e48-8c74-91f4f952ec2d"},"path":[{"$kind":"property","$content":1}]},"value":{"$kind":"value","$content":{"$kind":"vec3","$content":[7,8,9]}}},{)");
  const auto bad_type =
      replace_once(std::string(prefab_scene), "69644f20-b2d2-4e48-8c74-91f4f952ec2d",
                   "69644F20-b2d2-4e48-8c74-91f4f952ec2d");
  const auto legacy_type =
      replace_once(std::string(prefab_scene), "69644f20-b2d2-4e48-8c74-91f4f952ec2d",
                   "69644f20b2d24e488c7491f4f952ec2d");
  if (!fails_with(legacy_type, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/key/target/target_type_guid"))
    return 4;
  const auto bad_value =
      replace_once(std::string(prefab_scene), R"("$content":[4,5,6])", R"("$content":[4,5])");
  if (!fails_with(duplicate_override, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides") ||
      !fails_with(bad_type, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/key/target/target_type_guid") ||
      !fails_with(bad_value, gneiss::engine::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/value/$content/$content")) {
    return 4;
  }

  constexpr std::array<std::string_view, 11> other_values{
      R"("$kind":"bool","$content":true)",
      R"("$kind":"int64","$content":-5)",
      R"("$kind":"uint64","$content":5)",
      R"("$kind":"float32","$content":1.25)",
      R"("$kind":"float64","$content":2.5)",
      R"("$kind":"string","$content":"text")",
      R"("$kind":"guid","$content":"69644f20-b2d2-4e48-8c74-91f4f952ec2d")",
      R"("$kind":"guid","$content":null)",
      R"("$kind":"vec2","$content":[1,2])",
      R"("$kind":"vec4","$content":[1,2,3,4])",
      R"("$kind":"quaternion","$content":[0.1,0.2,0.3,0.9])",
  };
  for (const auto value : other_values) {
    const auto candidate =
        replace_once(std::string(prefab_scene), R"("$kind":"vec3","$content":[4,5,6])", value);
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(candidate),
            scene, diagnostic) != gneiss::engine::error::success ||
        gneiss::test_support::assign_value(gneiss::engine::asset_file::serialize(scene),
                                           serialized) != gneiss::engine::error::success) {
      return 5;
    }
    gneiss::engine::scene_description roundtrip;
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<gneiss::engine::scene_description>(serialized),
            roundtrip, diagnostic)
            .failed() ||
        roundtrip.objects[1].prefab->overrides[0].value !=
            scene.objects[1].prefab->overrides[0].value)
      return 5;
    if (value.find("quaternion") != std::string_view::npos) {
      const auto& q = std::get<gneiss::engine::math::quaternion>(
          std::get<gneiss::engine::value>(roundtrip.objects[1].prefab->overrides[0].value).payload);
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
