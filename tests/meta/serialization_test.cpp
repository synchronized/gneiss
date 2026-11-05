// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/serialization.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/material/material_description.hpp"
#include <array>
#include <limits>
#include <stdexcept>
namespace e = gneiss::engine;
std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
struct probe {
  inline static constexpr std::string_view type_name = "codec_probe";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({7, 6, 5})};
  int number = 3;
};
e::result<void*, e::serialization::diagnostic> bad_encode(const e::serialization::context&,
                                                          e::serialization::writer&, const probe&) {
  throw std::bad_alloc{};
}
e::result<probe, e::serialization::diagnostic> bad_decode(const e::serialization::context&,
                                                          e::serialization::reader&, const void*) {
  return e::serialization::diagnostic{.error = e::error::invalid_argument};
}
struct move_only {
  inline static constexpr std::string_view type_name = "move_only_codec";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({7, 6, 6})};
  std::unique_ptr<int> number;
  explicit move_only(int value) : number(std::make_unique<int>(value)) {}
  move_only(move_only&&) noexcept = default;
  move_only& operator=(move_only&&) = delete;
};
e::result<move_only, e::serialization::diagnostic>
decode_move_only(const e::serialization::context&, e::serialization::reader& input,
                 const void* node) {
  auto number = input.member(node, "number");
  if (!number)
    return e::serialization::diagnostic{3, "/number", "缺少数据", e::error::invalid_argument};
  auto decoded = input.scalar(number, e::value_kind::int64);
  if (!decoded)
    return std::move(decoded).error();
  return move_only{static_cast<int>(std::get<std::int64_t>(decoded->payload))};
}
int main() {
  // Meta 独立注册表即可读写 value，不依赖 World 或资源注册。
  auto core = e::meta::create_registry();
  if (!core || e::meta::register_builtin_types(*core).failed() ||
      e::meta::register_builtin_types(*core) != e::error::invalid_argument ||
      e::meta::freeze(*core).failed())
    return 30;
  e::math::matrix3 matrix3{};
  e::math::matrix4 matrix4{};
  for (std::size_t i = 0; i < 9; ++i)
    e::math::components(matrix3)[i] = float(i + 1);
  for (std::size_t i = 0; i < 16; ++i)
    e::math::components(matrix4)[i] = float(i + 1);
  const std::array cases{e::value{},
                         e::value{true},
                         e::value{std::int64_t{-2}},
                         e::value{std::uint64_t{2}},
                         e::value{1.5F},
                         e::value{2.5},
                         e::value{std::string{"abc"}},
                         e::value{e::guid{}},
                         e::value{e::math::vector2{1, 2}},
                         e::value{e::math::vector3{1, 2, 3}},
                         e::value{e::math::vector4{1, 2, 3, 4}},
                         e::value{e::math::quaternion_xyzw(1, 2, 3, 4)},
                         e::value{matrix3},
                         e::value{matrix4},
                         e::value{e::ref<e::meta::object>{}}};
  for (const auto& item : cases) {
    auto wire = e::serialization::json::serialize(e::serialization::context{*core}, item);
    if (!wire || wire->find("\"$kind\"") == std::string::npos ||
        wire->find("\"$content\"") == std::string::npos)
      return 40;
    auto restored = e::serialization::json::deserialize<e::value>(e::serialization::context{*core},
                                                                  std::string_view{*wire});
    if (!restored || *restored != item)
      return 41;
  }
  auto matrix_wire =
      e::serialization::json::serialize(e::serialization::context{*core}, e::value{matrix3});
  if (!matrix_wire ||
      matrix_wire->find("[1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0]") == std::string::npos)
    return 42;
  auto bad_matrix = e::serialization::json::deserialize<e::value>(
      e::serialization::context{*core},
      std::string_view{R"({"$kind":"matrix3","$content":[1,2]})"});
  if (bad_matrix)
    return 43;
  auto old_format = e::serialization::json::deserialize<e::value>(
      e::serialization::context{*core}, std::string_view{R"({"kind":"bool","value":true})"});
  if (old_format)
    return 44;
  e::math::components(matrix4)[2] = std::numeric_limits<float>::infinity();
  if (e::serialization::json::serialize(e::serialization::context{*core}, e::value{matrix4}))
    return 45;
  e::value edited{true};
  auto value_view = e::meta::mutable_instance::from(*core, edited);
  if (!value_view || value_view->kind() != e::meta::type_kind::variant)
    return 46;
  auto variant = value_view->as_variant();
  if (!variant || variant->alternative_count() != cases.size() || variant->select(12).failed())
    return 47;
  auto selected = variant->value();
  if (!selected || selected->write_value(e::value{matrix3}).failed() || edited != e::value{matrix3})
    return 48;
  if (variant->select(0).failed() || edited.kind() != e::value_kind::invalid)
    return 49;
  if (variant->select(cases.size()) != e::error::invalid_argument)
    return 50;
  const e::value source{std::string{"独立 Meta"}};
  auto encoded = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*core}, source);
  if (!encoded)
    return 31;
  auto restored_value = gneiss::engine::serialization::json::deserialize<e::value>(
      gneiss::engine::serialization::context{*core}, *encoded);
  if (!restored_value || *restored_value != source)
    return 32;
  // 显式 JSON 文本/字节入口共享格式，独立注册表不会退回默认表。
  auto text =
      e::serialization::json::serialize(gneiss::engine::serialization::context{*core}, source);
  if (!text ||
      *text != std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size()))
    return 34;
  auto from_text = e::serialization::json::deserialize<e::value>(
      gneiss::engine::serialization::context{*core}, std::string_view{*text});
  if (!from_text || *from_text != source)
    return 35;
  auto invalid = gneiss::engine::serialization::json::serialize_bytes(
      gneiss::engine::serialization::context{*core}, e::value{});
  if (!invalid || e::meta::register_builtin_types(*core) != e::error::invalid_state)
    return 33;

  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::serialization::diagnostic diagnostic;
  e::material_description material;
  std::vector<std::byte> output{std::byte{42}};
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_handle ||
      output.size() != 1)
    return 1;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::default_registry(), registry)
          .failed() ||
      e::meta::register_builtin_types().failed() || e::register_reflection().failed() ||
      e::register_builtin_resource_types().failed())
    return 2;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::not_ready)
    return 3;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<nullptr, nullptr>()
          .commit() != e::error::invalid_argument)
    return 4;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<static_cast<decltype(&bad_encode)>(nullptr),
                         static_cast<decltype(&bad_decode)>(nullptr)>()
          .commit() != e::error::invalid_argument)
    return 25;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<&bad_encode, &bad_decode>()
          .serialization<&bad_encode, &bad_decode>()
          .commit() != e::error::invalid_argument)
    return 5;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<&bad_encode, &bad_decode>()
          .commit()
          .failed())
    return 5;
  if (e::meta::type_builder<move_only>{}
          .serialization<nullptr, &decode_move_only>()
          .commit()
          .failed())
    return 24;
  if (e::register_builtin_resource_types() != e::error::invalid_argument ||
      gneiss::engine::meta::freeze(registry).failed())
    return 6;
  gneiss::engine::serialization::binding binding;
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::find_serialization(registry, e::runtime_type_of<probe>()), binding)
          .failed() ||
      !binding.encode || !binding.decode)
    return 7;
  probe object;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::serialize_bytes(
              gneiss::engine::serialization::context{registry}, object),
          output, diagnostic) != e::error::out_of_memory ||
      output.size() != 1 || output[0] != std::byte{42})
    return 8;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::deserialize<std::remove_cvref_t<decltype(object)>>(
              gneiss::engine::serialization::context{registry}, bytes("bad")),
          object, diagnostic) != e::error::invalid_argument ||
      object.number != 3)
    return 9;
  material.parameters.color = {0.25F, 0.5F, 0.75F, 1};
  material.parameters.metallic = 0.5F;
  material.parameters.alpha_mode = e::material_alpha_mode::mask;
  material.parameters.double_sided = true;
  material.texture_uris[0] = "asset://textures/纹理\".json";
  material.parameters.sampling[0].uv_set = 1;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic)
          .failed())
    return 10;
  e::material_description restored;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
              gneiss::engine::serialization::context{registry}, output),
          restored, diagnostic)
          .failed() ||
      restored.parameters.color != material.parameters.color ||
      restored.texture_uris != material.texture_uris || restored.parameters.metallic != 0.5F ||
      restored.parameters.alpha_mode != e::material_alpha_mode::mask ||
      !restored.parameters.double_sided || restored.parameters.sampling[0].uv_set != 1)
    return 11;
  const auto saved = output;
  material.parameters.color[0] = std::numeric_limits<float>::infinity();
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 12;
  material.parameters.color[0] = 2;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::serialize_bytes(
              gneiss::engine::serialization::context{registry}, material),
          output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 13;
  for (unsigned version : {1U, 2U, 3U, 4U, 5U, 7U}) {
    const auto text = std::string{R"({"format":"gneiss.material","version":)"} +
                      std::to_string(version) + R"(,"color":[1,1,1,1]})";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
                gneiss::engine::serialization::context{registry}, bytes(text)),
            restored, diagnostic) != e::error::unsupported ||
        restored.parameters.color[0] != 0.25F)
      return 14;
  }
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::asset_file::deserialize<std::remove_cvref_t<decltype(restored)>>(
              gneiss::engine::serialization::context{registry}, bytes("{")),
          restored, diagnostic) != e::error::invalid_argument ||
      restored.parameters.color[0] != 0.25F)
    return 15;
  if (e::register_builtin_resource_types() != e::error::invalid_state)
    return 16;
  gneiss::engine::meta::registry_ref no_codec;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::create_registry(), no_codec)
          .failed() ||
      gneiss::engine::meta::type_builder<probe>{no_codec}.commit().failed() ||
      gneiss::engine::meta::freeze(no_codec).failed())
    return 17;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::serialization::json::serialize_bytes(
              gneiss::engine::serialization::context{no_codec}, object),
          output, diagnostic) != e::error::unsupported ||
      output != saved)
    return 18;
  auto owned_registry = e::meta::create_registry();
  if (!owned_registry ||
      e::meta::type_builder<move_only>{*owned_registry}
          .serialization<nullptr, &decode_move_only>()
          .commit()
          .failed() ||
      e::meta::freeze(*owned_registry).failed())
    return 19;
  auto owned = gneiss::engine::serialization::json::deserialize<move_only>(
      gneiss::engine::serialization::context{*owned_registry}, bytes(R"({"number":65})"));
  if (!owned || !owned->number || *owned->number != 65)
    return 20;
  auto rejected = gneiss::engine::serialization::json::deserialize<move_only>(
      gneiss::engine::serialization::context{*owned_registry}, bytes("{}"));
  if (rejected || rejected.error().error != e::error::invalid_argument ||
      rejected.error().byte_offset != 3 || rejected.error().path != "/number")
    return 21;
  // 反射创建的拥有者可共享/擦除为 const；借用视图不承担所有权。
  auto shared_object = gneiss::engine::serialization::json::deserialize_object(
      gneiss::engine::serialization::context{*owned_registry}, e::runtime_type_of<move_only>(),
      bytes(R"({"number":65})"));
  if (!shared_object || !*shared_object)
    return 24;
  std::weak_ptr<void> weak_object = shared_object->shared();
  e::ref<const void> kept = *shared_object;
  shared_object->reset();
  if (weak_object.expired() || !kept)
    return 25;
  kept.reset();
  if (!weak_object.expired())
    return 26;
  // value 使用通用 codec，也可以作为 JSON 资产解码目标。
  auto value_decoder = e::detail::make_reflected_decoder<e::value>();
  if (!value_decoder)
    return 36;
  auto decoded_value = (*value_decoder)->decode(*encoded);
  if (!decoded_value || *std::static_pointer_cast<const e::value>(*decoded_value) != source)
    return 37;
  auto decoder = e::detail::make_reflected_decoder<move_only>();
  if (!decoder)
    return 22;
  auto input = bytes(R"({"number":42})");
  auto decoded = (*decoder)->decode(std::vector<std::byte>(input.begin(), input.end()));
  if (!decoded || *std::static_pointer_cast<const move_only>(*decoded)->number != 42)
    return 23;
  return 0;
}
