// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include "engine/resource/scene/scene_record.hpp"
#include "engine/resource/scene/scene_validation.hpp"

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
  static constexpr gneiss::guid type_guid = gneiss::guid::from_bytes({98, 11});
  std::string text;
};
constexpr gneiss::asset_file::format probe_format{"probe", 1, 80};
bool format_node_contract() {
  namespace e = gneiss;
  namespace m = e::meta;
  auto registry = m::create_registry();
  if (!registry ||
      m::registration::type<format_probe>(*registry)
          .property<&format_probe::text>(m::property_id{1}, "text")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit()
          .failed() ||
      m::freeze(*registry).failed())
    return false;
  e::serialization::write_context ctx{*registry};
  e::serialization::read_context read_ctx{*registry};

  const auto encode = [&](const format_probe& object) {
    auto view = m::view(*registry, std::as_const(object));
    return e::asset_file::serialize_instance(ctx, probe_format, *view);
  };
  const auto decode = [&](std::span<const std::byte> input) {
    return e::asset_file::deserialize_object(read_ctx, probe_format,
                                             e::runtime_type_of<format_probe>(), input);
  };
  auto encoded = encode(format_probe{"ok"});
  if (!encoded)
    return false;
  auto decoded = decode(*encoded);
  if (!decoded || static_cast<format_probe*>(decoded->get())->text != "ok")
    return false;
  auto plain = e::serialization::json::encode(ctx, format_probe{"ok"});
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
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0101","$content":{"settings":{"fov":1.0,"near":0.1,"far":100},"is_primary":true,"uuid":"2ceec979-6250-498c-966a-3f5923c9e17f"}}]},{"uuid":"00000000-0000-4000-8000-000000000003","parent":{"object":"00000000-0000-4000-8000-000000000002","source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material","uuid":"852fef5e-cc4e-4571-a4b8-b933c063677a"}}]}],"unknown":true})";

constexpr std::string_view prefab_scene =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]},{"name":"Lamp","parent":{"object":"00000000-0000-4000-8000-000000000002","source":null},"prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"key":{"target":{"source_node_uuid":"10000000-0000-4000-8000-000000000002","target_type_guid":"69644f20-b2d2-4e48-8c74-91f4f952ec2d","component_uuid":null},"path":[{"$kind":"property","$content":1}]},"operation":{"$kind":"set","$content":{"content":{"$kind":"vec3","$content":[4,5,6]}}}}]},"transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[2,2,2]},"unknown":true,"uuid":"00000000-0000-4000-8000-000000000010"}]})";

[[nodiscard]] std::string replace_once(std::string text, std::string_view source,
                                       std::string_view replacement) {
  const auto position = text.find(source);
  if (position != std::string::npos) {
    text.replace(position, source.size(), replacement);
  }
  return text;
}

[[nodiscard]] bool fails_with(std::string_view json, gneiss::error expected,
                              std::string_view path) {
  gneiss::scene_description scene;
  gneiss::serialization::diagnostic diagnostic;
  const auto status = gneiss::test_support::assign_decoded(
      gneiss::asset_file::deserialize<gneiss::scene_description>(json), scene,
      diagnostic);
  const bool matched = status == expected && diagnostic.error == expected &&
                       diagnostic.path == path && scene.objects.empty();
  if (!matched)
    std::fprintf(stderr, "expected path %.*s, actual %s\n", static_cast<int>(path.size()),
                 path.data(), diagnostic.path.c_str());
  return matched;
}

bool component_validation_contract() {
  namespace e = gneiss;
  const auto rejects_domain = [](const e::component_description& input) {
    // 通用快照与 JSON 允许尚未采用的描述，领域边界负责拒绝。
    auto snapshot = e::serialization::capture_value(input);
    auto text = e::serialization::json::encode_bytes(input);
    if (!snapshot || !text)
      return false;
    auto decoded = e::serialization::json::decode<e::component_description>(*text);
    auto restored = e::serialization::restore_value<e::component_description>(*snapshot);
    if (!decoded || !restored || e::validate_component_description(*decoded).error.ok() ||
        e::validate_component_description(*restored).error.ok())
      return false;
    e::prefab_component_override added{.source_node_uuid = e::guid::generate(),
                                       .type_guid = input->guid(),
                                       .added = input,
                                       .component_uuid = input->uuid};
    return e::validate_component_override(added).error == e::error::invalid_argument;
  };
  return rejects_domain(e::make_component_description(e::camera_description{{1, 10, 1}})) &&
         rejects_domain(e::make_component_description(e::mesh_renderer_description{"bad", ""}));
}
bool save_validation_contract() {
  namespace e = gneiss;
  auto parsed = gneiss::asset_file::deserialize<e::scene_description>(valid_scene);
  auto prefab = gneiss::asset_file::deserialize<e::scene_description>(prefab_scene);
  if (!parsed || !prefab)
    return false;
  const auto rejects = [&](auto change) {
    auto candidate = *parsed;
    change(candidate);
    auto text = gneiss::asset_file::serialize(candidate);
    auto bytes = gneiss::asset_file::serialize_bytes(candidate);
    return !text && text.error().error == e::error::invalid_argument && !bytes &&
           bytes.error().error == e::error::invalid_argument;
  };
  if (!rejects([](auto& scene) { scene.objects[0].parent.object = scene.objects[1].uuid; }) ||
      !rejects([](auto& scene) { scene.objects[1].uuid = scene.objects[0].uuid; }) ||
      !rejects([](auto& scene) {
        scene.objects[0].transform.translation.x = std::numeric_limits<float>::infinity();
      }) ||
      !rejects([](auto& scene) { scene.objects[0].transform.rotation.w = 2.0F; }) ||
      !rejects([](auto& scene) { scene.objects[0].transform.scale.y = 0.0F; }) ||
      !rejects([](auto& scene) {
        auto camera = *scene.objects[0].components.template get<e::camera_description>();
        camera.settings.near = camera.settings.far;
        scene.objects[0].components.set(std::move(camera));
      }) ||
      !rejects([](auto& scene) {
        auto mesh = *scene.objects[1].components.template get<e::mesh_renderer_description>();
        mesh.mesh_uri = "bad";
        scene.objects[1].components.set(std::move(mesh));
      }))
    return false;
  const auto rejects_override = [&](auto change) {
    auto candidate = *prefab;
    change(*candidate.objects[1].prefab);
    auto encoded = gneiss::asset_file::serialize_bytes(candidate);
    return !encoded && encoded.error().error == e::error::invalid_argument;
  };
  if (!rejects_override([](auto& reference) { reference.uri = "bad"; }) ||
      !rejects_override(
          [](auto& reference) { reference.overrides[0].key.target.target_type_guid = {}; }) ||
      !rejects_override([](auto& reference) { reference.overrides[0].key.path.front() = {}; }) ||
      !rejects_override([](auto& reference) {
        reference.overrides[0].replacement().payload = std::numeric_limits<double>::quiet_NaN();
      }) ||
      !rejects_override([](auto& reference) {
        reference.overrides[0].replacement().payload = e::ref<e::meta::object>{};
      }))
    return false;
  for (const e::value& vector :
       {e::value{e::math::vector2{1, 2}}, e::value{e::math::vector4{1, 2, 3, 4}}}) {
    auto candidate = *prefab;
    candidate.objects[1].prefab->overrides[0].operation = vector;
    auto encoded = gneiss::asset_file::serialize_bytes(candidate);
    if (!encoded)
      return false;
    auto decoded = gneiss::asset_file::deserialize<e::scene_description>(*encoded);
    if (!decoded || decoded->objects[1].prefab->overrides[0].replacement() != vector)
      return false;
  }
  if (!rejects_override([](auto& reference) {
        reference.overrides[0].operation =
            e::value{e::math::vector2{1, std::numeric_limits<float>::infinity()}};
      }))
    return false;
  // Scene 仍允许多根，不因 Prefab 的唯一根约束变成只能保存一棵树。
  parsed->objects[1].parent.object = {};
  auto forest = gneiss::asset_file::serialize_bytes(*parsed);
  return forest &&
         static_cast<bool>(gneiss::asset_file::deserialize<e::scene_description>(*forest));
}

bool guid_identity_contract() {
  namespace e = gneiss;
  auto parsed = gneiss::asset_file::deserialize<e::scene_description>(valid_scene);
  if (!parsed || parsed->objects[0].parent.object ||
      parsed->objects[1].parent.object != parsed->objects[0].uuid)
    return false;
  const auto original = *parsed;
  // 内存身份修改必须反映到文件；不沿用 输入文件中的旧场景 UUID。
  parsed->uuid = *e::guid::parse("abcdef01-2345-4678-9abc-def012345678");
  auto encoded = gneiss::asset_file::serialize(*parsed);
  if (!encoded)
    return false;
  auto restored = gneiss::asset_file::deserialize<e::scene_description>(*encoded);
  if (!restored || restored->uuid != parsed->uuid ||
      restored->objects[1].parent.object != parsed->objects[0].uuid)
    return false;
  parsed->objects[0].parent.object = e::guid{};
  if (!gneiss::asset_file::serialize(*parsed))
    return false;
  *parsed = original;
  parsed->objects[0].uuid = {};
  if (gneiss::asset_file::serialize(*parsed))
    return false;
  *parsed = original;
  parsed->uuid = {};
  if (gneiss::asset_file::serialize(*parsed))
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
      replace_once(std::string(valid_scene),
                   R"("parent":{"object":"00000000-0000-4000-8000-000000000002","source":null})",
                   R"("parent":{"object":null,"source":null})");
  if (!gneiss::asset_file::deserialize<e::scene_description>(nil_parent))
    return false;
  const auto null_identity =
      replace_once(std::string(valid_scene), R"("uuid":"00000000-0000-4000-8000-000000000002")",
                   R"("uuid":null)");
  if (!fails_with(null_identity, e::error::invalid_argument, "/objects/0/uuid"))
    return false;
  const auto nil_source =
      replace_once(std::string(prefab_scene), "10000000-0000-4000-8000-000000000002",
                   "00000000-0000-0000-0000-000000000000");
  if (gneiss::asset_file::deserialize<e::scene_description>(nil_source))
    return false;
  const auto low = *e::guid::parse("00ffffff-ffff-4fff-8fff-ffffffffffff");
  const auto high = *e::guid::parse("01000000-0000-4000-8000-000000000000");
  return low < high && low.string() < high.string() &&
         e::guid_hash{}(low) == e::guid_hash{}(*e::guid::parse(low.string()));
}

class memory_file_system final : public gneiss::test_support::memory_file_fixture {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss::error
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "main.scene.json") {
      return gneiss::error::not_found;
    }
    try {
      out_bytes.resize(content_.size());
      for (std::size_t index = 0U; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return gneiss::error::success;
    } catch (...) {
      return gneiss::error::out_of_memory;
    }
  }

  gneiss::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& output) const noexcept override {
    if (content_.size() > limit)
      return gneiss::error::invalid_argument;
    return read(path, output);
  }

private:
  std::string content_;
};

bool domain_validation_boundary_contract() {
  namespace e = gneiss;
  e::scene_description scene; // nil 身份可编码，但不是可采用的场景。
  auto plain = e::serialization::json::encode(scene);
  if (!plain || !e::serialization::json::decode<e::scene_description>(*plain))
    return false;
  auto invalid_save = e::asset_file::serialize(scene);
  auto invalid_load = e::asset_file::deserialize<e::scene_description>(
      R"({"format":"gneiss.scene","version":5,"scene_uuid":null,"objects":[]})");
  if (invalid_save || invalid_load || invalid_save.error().path != "/scene_uuid" ||
      invalid_load.error().path != "/scene_uuid" ||
      e::restore_scene_record(e::scene_record{e::scene_record_data{}}))
    return false;
  e::prefab_description prefab;
  prefab.uuid = e::guid::generate();
  auto prefab_plain = e::serialization::json::encode(prefab);
  if (!prefab_plain || !e::serialization::json::decode<e::prefab_description>(*prefab_plain))
    return false;
  auto prefab_save = e::asset_file::serialize(prefab);
  auto prefab_text = *prefab_plain;
  prefab_text.pop_back();
  prefab_text += R"(,"format":"gneiss.prefab","version":1})";
  auto prefab_load = e::asset_file::deserialize<e::prefab_description>(prefab_text);
  if (prefab_save || prefab_load || prefab_save.error().path != "/root_uuid" ||
      prefab_load.error().path != "/root_uuid" ||
      e::restore_prefab_record(e::prefab_record{e::prefab_record_data{prefab.uuid, {}, {}}}))
    return false;
  // 局部对象和 Prefab 引用也由领域递归校验；通用内容可以暂时不合法。
  scene.uuid = e::guid::generate();
  scene.objects.emplace_back();
  plain = e::serialization::json::encode(scene);
  auto local_save = e::asset_file::serialize(scene);
  if (!plain || !e::serialization::json::decode<e::scene_description>(*plain) || local_save ||
      local_save.error().path != "/objects/0/uuid")
    return false;
  scene.objects[0].uuid = e::guid::generate();
  scene.objects[0].prefab.emplace();
  scene.objects[0].prefab->uri = "invalid";
  plain = e::serialization::json::encode(scene);
  local_save = e::asset_file::serialize(scene);
  if (!plain || !e::serialization::json::decode<e::scene_description>(*plain) || local_save ||
      local_save.error().path != "/objects/0/prefab/uri")
    return false;
  scene.objects.clear();
  // 层级约束也由领域入口负责，而不是编码对象图的过程中顺带检查。
  scene.uuid = e::guid::generate();
  e::game_object_description object;
  object.uuid = e::guid::generate();
  scene.objects = {object, object};
  plain = e::serialization::json::encode(scene);
  return plain && e::serialization::json::decode<e::scene_description>(*plain) &&
         !e::asset_file::serialize(scene);
}
} // namespace

int main() try {
  if (!format_node_contract())
    return 97;
  if (gneiss::initialize_meta().failed())
    return 99;
  if (!domain_validation_boundary_contract())
    return 96;
  if (!component_validation_contract())
    return 88;
  if (!save_validation_contract())
    return 11;
  if (!guid_identity_contract())
    return 98;
  {
    auto fresh =
        gneiss::asset_file::deserialize<gneiss::scene_description>(valid_scene);
    if (!fresh)
      return 40;
    auto bytes = gneiss::asset_file::serialize_bytes(*fresh);
    if (!bytes)
      return 41;
    auto restored =
        gneiss::asset_file::deserialize<gneiss::scene_description>(*bytes);
    auto snapshot =
        gneiss::asset_file::deserialize_ref<gneiss::scene_description>(*bytes);
    gneiss::serialization::read_context read_context;
    auto independent =
        gneiss::asset_file::deserialize_ref<gneiss::scene_description>(read_context,
                                                                                       *bytes);
    if (!snapshot || !independent || snapshot->get() == independent->get() ||
        (*snapshot)->uuid != fresh->uuid || (*snapshot)->objects.size() != fresh->objects.size())
      return 43;
    auto retained = *snapshot;
    snapshot =
        gneiss::serialization::diagnostic{.error = gneiss::error::invalid_state};
    bytes->clear();
    if (retained->uuid != fresh->uuid || retained->objects.size() != fresh->objects.size())
      return 44;
    auto invalid =
        gneiss::asset_file::deserialize_ref<gneiss::scene_description>(*bytes);
    if (invalid)
      return 45;
    if (!restored || restored->uuid != fresh->uuid ||
        restored->objects.size() != fresh->objects.size())
      return 42;
  }
  // 反射 codec 忽略未知字段；解码失败不覆盖旧对象。
  {
    namespace e = gneiss;
    auto text = std::string(valid_scene);
    text.insert(1, R"("codec_unknown":{"keep":7},)");
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    e::scene_description value;
    gneiss::serialization::diagnostic detail;
    std::vector<std::byte> encoded;
    if (gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<std::remove_cvref_t<decltype(value)>>(source),
            value, detail)
            .failed() ||
        gneiss::test_support::assign_decoded(gneiss::asset_file::serialize_bytes(value),
                                             encoded, detail)
            .failed())
      return 91;
    e::scene_description restored;
    if (gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                encoded),
            restored, detail)
            .failed() ||
        restored.objects.size() != value.objects.size() ||
        (*gneiss::asset_file::serialize(restored)).find("codec_unknown") !=
            std::string::npos)
      return 92;
    const auto saved = (*gneiss::asset_file::serialize(restored));
    const std::string broken = "{";
    if (gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                std::as_bytes(std::span{broken.data(), broken.size()})),
            restored, detail)
            .ok() ||
        (*gneiss::asset_file::serialize(restored)) != saved)
      return 93;
    std::vector<std::byte> oversized(e::scene_description::maximum_source_bytes + 1U);
    if (gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                oversized),
            restored, detail) != e::error::invalid_argument ||
        (*gneiss::asset_file::serialize(restored)) != saved)
      return 94;
  }

  gneiss::scene_description scene;
  gneiss::serialization::diagnostic diagnostic;
  std::string serialized;
  if (gneiss::test_support::assign_decoded(
          gneiss::asset_file::deserialize<gneiss::scene_description>(valid_scene),
          scene, diagnostic) != gneiss::error::success ||
      scene.objects.size() != 2U ||
      !scene.objects[0].components.template get<gneiss::camera_description>() ||
      !scene.objects[1].components.template get<gneiss::mesh_renderer_description>() ||
      gneiss::test_support::assign_value(gneiss::asset_file::serialize(scene),
                                         serialized) != gneiss::error::success ||
      serialized.find(R"("unknown":true)") != std::string::npos) {
    return 1;
  }

  const auto legacy_array =
      replace_once(std::string(valid_scene), "{", R"({"prefab_instances":[],)");
  if (!fails_with(legacy_array, gneiss::error::invalid_argument, "/prefab_instances"))
    return 95;

  for (auto version : {"0", "-1", "1.5", "null", "\"5\""}) {
    auto malformed = replace_once(std::string(valid_scene), "\"version\":5",
                                  std::string("\"version\":") + version);
    if (!fails_with(malformed, gneiss::error::invalid_argument, "/version"))
      return 112;
  }
  for (auto header : {R"({"format":"gneiss.scene",)", R"({"version":5,)"}) {
    if (!fails_with(replace_once(std::string(valid_scene), "{", header),
                    gneiss::error::invalid_argument, ""))
      return 113;
  }
  const auto future = replace_once(std::string(valid_scene), "\"version\":5", "\"version\":6");
  const auto old = replace_once(std::string(valid_scene), "\"version\":5", "\"version\":4");
  const auto duplicate =
      replace_once(std::string(valid_scene), "00000000-0000-4000-8000-000000000003",
                   "00000000-0000-4000-8000-000000000002");
  const auto missing_parent =
      replace_once(std::string(valid_scene),
                   R"("parent":{"object":"00000000-0000-4000-8000-000000000002","source":null})",
                   R"("parent":{"object":"00000000-0000-4000-8000-000000000099","source":null})");
  const auto cycle =
      replace_once(std::string(valid_scene), R"("parent":{"object":null,"source":null})",
                   R"("parent":{"object":"00000000-0000-4000-8000-000000000003","source":null})");
  const auto invalid_uri = replace_once(std::string(valid_scene), "asset://models/triangle.mesh",
                                        "asset://models/../triangle.mesh");
  // 单位变换默认值不允许掩盖文件中缺失的必填分量。
  for (const auto field :
       {R"("translation":[0,1,2],)", R"("rotation":[0,0,0,1],)", R"(,"scale":[1,1,1])"}) {
    auto missing = replace_once(std::string(valid_scene), field, "");
    auto parsed =
        gneiss::asset_file::deserialize<gneiss::scene_description>(missing);
    if (parsed || parsed.error().error != gneiss::error::invalid_argument ||
        !parsed.error().path.starts_with("/objects/0/transform/"))
      return 96;
  }
  const auto invalid_scale =
      replace_once(std::string(valid_scene), R"("scale":[1,1,1])", R"("scale":[1,0,1])");
  const auto invalid_rotation =
      replace_once(std::string(valid_scene), R"("rotation":[0,0,0,1])", R"("rotation":[0,0,0,2])");
  if (!fails_with(future, gneiss::error::unsupported, "/version") ||
      !fails_with(old, gneiss::error::unsupported, "/version") ||
      !fails_with(duplicate, gneiss::error::invalid_argument, "/objects/1/uuid") ||
      !fails_with(missing_parent, gneiss::error::invalid_argument, "/objects/1/parent") ||
      !fails_with(cycle, gneiss::error::invalid_argument, "/objects/0/parent") ||
      !fails_with(invalid_uri, gneiss::error::invalid_argument,
                  "/objects/1/components/0") ||
      !fails_with(invalid_scale, gneiss::error::invalid_argument,
                  "/objects/0/transform/scale") ||
      !fails_with(invalid_rotation, gneiss::error::invalid_argument,
                  "/objects/0/transform/rotation")) {
    return 2;
  }

  if (gneiss::test_support::assign_decoded(
          gneiss::asset_file::deserialize<gneiss::scene_description>(prefab_scene),
          scene, diagnostic) != gneiss::error::success ||
      scene.objects.size() != 2U || scene.objects[1].prefab->overrides.size() != 1U ||
      std::get<gneiss::math::vector3>(
          scene.objects[1].prefab->overrides[0].replacement().payload)[0] != 4.0F ||
      gneiss::test_support::assign_value(gneiss::asset_file::serialize(scene),
                                         serialized) != gneiss::error::success ||
      serialized.find(R"("$kind":"vec3")") == std::string::npos ||
      serialized.find(R"("unknown":true)") != std::string::npos) {
    return 3;
  }

  {
    using namespace gneiss;
    auto null_prefab = gneiss::asset_file::deserialize<scene_description>(replace_once(
        std::string(valid_scene), R"("components":[)", R"("prefab":null,"components":[)"));
    if (!null_prefab || null_prefab->objects[0].prefab)
      return 97;
    auto slot = meta::view(std::as_const(scene.objects[1]));
    if (!slot)
      return 98;
    auto reference = slot->member("prefab")->as_optional();
    if (!reference || !*reference->has_value())
      return 98;
    auto contents = reference->get();
    if (!contents || contents->properties().size() != 3 ||
        *contents->member("overrides")->as_sequence()->size() != 1)
      return 98;
    auto copied = scene.objects[1];
    copied.prefab->uri = "asset://prefabs/copy.prefab.json";
    copied.prefab->overrides.clear();
    if (scene.objects[1].prefab->overrides.empty() ||
        scene.objects[1].prefab->uri == copied.prefab->uri)
      return 99;
    auto bytes = gneiss::serialization::json::encode_bytes(*scene.objects[1].prefab);
    if (!bytes)
      return 100;
    auto restored =
        gneiss::serialization::json::decode<prefab_reference_description>(*bytes);
    if (!restored || restored->overrides.size() != 1 ||
        restored->uri != scene.objects[1].prefab->uri)
      return 100;
    auto conflict = *restored;
    conflict.component_overrides.push_back(
        {.source_node_uuid = conflict.overrides[0].key.target.source_node_uuid,
         .type_guid = conflict.overrides[0].key.target.target_type_guid});
    if (!gneiss::serialization::json::encode_bytes(conflict) ||
        validate_prefab_reference_description(conflict).error.ok())
      return 101;
    conflict = *restored;
    conflict.uri = "asset://../bad";
    auto rejected = validate_prefab_reference_description(conflict);
    if (rejected.error.ok() || rejected.path != "/uri" ||
        !gneiss::serialization::json::encode_bytes(conflict))
      return 102;
  }
  if (!fails_with(replace_once(std::string(prefab_scene),
                               R"("uri":"asset://prefabs/lamp.prefab.json")",
                               R"("uri":"asset://../bad")"),
                  gneiss::error::invalid_argument, "/objects/1/prefab/uri"))
    return 103;

  {
    using namespace gneiss;
    // 通用入口检查结构，领域入口检查对象语义；缺失组件默认空列表。
    const std::string object_json =
        R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})";
    auto decode = [](std::string_view text) {
      return gneiss::serialization::json::decode<game_object_description>(
          std::as_bytes(std::span{text.data(), text.size()}));
    };
    auto empty = decode(object_json);
    if (!empty || !empty->components.empty() || empty->prefab)
      return 104;
    auto empty_bytes = gneiss::serialization::json::encode_bytes(*empty);
    if (!empty_bytes)
      return 105;
    const std::string empty_json(reinterpret_cast<const char*>(empty_bytes->data()),
                                 empty_bytes->size());
    if (empty_json.find(R"("components":[])") == std::string::npos ||
        empty_json.find(R"("prefab":null)") == std::string::npos)
      return 105;
    auto template_bytes = gneiss::serialization::json::encode_bytes(scene.objects[1]);
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
    if (!invalid_uuid || validate_game_object_description(*invalid_uuid).path != "/uuid")
      return 107;
    empty->uuid = {};
    auto invalid_encoded = gneiss::serialization::json::encode_bytes(*empty);
    if (!invalid_encoded || validate_game_object_description(*empty).path != "/uuid")
      return 107;
    auto invalid_components = decode(replace_once(object_json, "{", R"({"components":{},)"));
    if (invalid_components || invalid_components.error().path != "/components")
      return 108;
    auto conflict = scene.objects[1];
    auto normal = gneiss::asset_file::deserialize<scene_description>(valid_scene);
    if (!normal)
      return 109;
    conflict.components = normal->objects[0].components;
    auto rejected = gneiss::serialization::json::encode_bytes(conflict);
    if (!rejected || validate_game_object_description(conflict).path != "/components")
      return 109;
    auto component_bytes = gneiss::serialization::json::encode_bytes(normal->objects[0]);
    if (!component_bytes)
      return 110;
    std::string component_json(reinterpret_cast<const char*>(component_bytes->data()),
                               component_bytes->size());
    component_json =
        replace_once(component_json, R"("prefab":null)",
                     R"("prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[]})");
    auto conflict_decoded = decode(component_json);
    if (!conflict_decoded ||
        validate_game_object_description(*conflict_decoded).path != "/components")
      return 110;
    auto missing = gneiss::asset_file::deserialize<scene_description>(
        replace_once(std::string(prefab_scene), R"(,"components":[])", ""));
    if (!missing || !missing->objects[0].components.empty())
      return 111;
  }

  const auto duplicate_override = replace_once(
      std::string(prefab_scene), R"("overrides":[{)",
      R"("overrides":[{"key":{"target":{"source_node_uuid":"10000000-0000-4000-8000-000000000002","target_type_guid":"69644f20-b2d2-4e48-8c74-91f4f952ec2d","component_uuid":null},"path":[{"$kind":"property","$content":1}]},"operation":{"$kind":"set","$content":{"content":{"$kind":"vec3","$content":[7,8,9]}}}},{)");
  const auto bad_type =
      replace_once(std::string(prefab_scene), "69644f20-b2d2-4e48-8c74-91f4f952ec2d",
                   "69644F20-b2d2-4e48-8c74-91f4f952ec2d");
  const auto legacy_type =
      replace_once(std::string(prefab_scene), "69644f20-b2d2-4e48-8c74-91f4f952ec2d",
                   "69644f20b2d24e488c7491f4f952ec2d");
  if (!fails_with(legacy_type, gneiss::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/key/target/target_type_guid"))
    return 4;
  const auto bad_value =
      replace_once(std::string(prefab_scene), R"("$content":[4,5,6])", R"("$content":[4,5])");
  if (!gneiss::asset_file::deserialize<gneiss::scene_description>(
          duplicate_override) ||
      !fails_with(bad_type, gneiss::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/key/target/target_type_guid") ||
      !fails_with(bad_value, gneiss::error::invalid_argument,
                  "/objects/1/prefab/overrides/0/operation/$content/content/$content")) {
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
            gneiss::asset_file::deserialize<gneiss::scene_description>(candidate),
            scene, diagnostic) != gneiss::error::success ||
        gneiss::test_support::assign_value(gneiss::asset_file::serialize(scene),
                                           serialized) != gneiss::error::success) {
      return 5;
    }
    gneiss::scene_description roundtrip;
    if (gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<gneiss::scene_description>(serialized),
            roundtrip, diagnostic)
            .failed() ||
        roundtrip.objects[1].prefab->overrides[0].replacement() !=
            scene.objects[1].prefab->overrides[0].replacement())
      return 5;
    if (value.find("quaternion") != std::string_view::npos) {
      const auto& q = std::get<gneiss::math::quaternion>(
          roundtrip.objects[1].prefab->overrides[0].replacement().payload);
      if (q.x != 0.1F || q.y != 0.2F || q.z != 0.3F || q.w != 0.9F)
        return 5;
    }
  }

  gneiss::test_support::fixture_files file_system;
  if (file_system.mount("asset://scenes/", std::make_shared<memory_file_system>(std::string(
                                               valid_scene))) != gneiss::error::success)
    return 6;
  gneiss::asset_manager assets(gneiss::test_support::test_files(file_system));
  gneiss::asset_ref<const gneiss::scene_description> loaded;
  if (gneiss::test_support::load_scene_snapshot(assets, "asset://scenes/main.scene.json", loaded)
          .failed() ||
      !loaded)
    return 6;
  return fails_with("{\"format\":", gneiss::error::invalid_argument, "") ? 0 : 7;
} catch (...) {
  return 99;
}
