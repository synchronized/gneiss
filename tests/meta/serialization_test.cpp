// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/material/material_codec.hpp"
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
e::result<std::vector<std::byte>, e::meta::serialization_diagnostic> bad_encode(const probe&) {
  throw std::bad_alloc{};
}
e::result<probe, e::meta::serialization_diagnostic> bad_decode(std::span<const std::byte>) {
  return e::meta::serialization_diagnostic{.error = e::error::invalid_argument};
}
struct move_only {
  inline static constexpr std::string_view type_name = "move_only_codec";
  inline static constexpr e::guid type_guid{e::guid::from_bytes({7, 6, 6})};
  std::unique_ptr<int> number;
  explicit move_only(int value) : number(std::make_unique<int>(value)) {}
  move_only(move_only&&) noexcept = default;
  move_only& operator=(move_only&&) = delete;
};
e::result<move_only, e::meta::serialization_diagnostic>
decode_move_only(std::span<const std::byte> input) {
  if (input.empty())
    return e::meta::serialization_diagnostic{3, "/number", "缺少数据", e::error::invalid_argument};
  return move_only{std::to_integer<int>(input.front())};
}
int main() {
  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::meta::serialization_diagnostic diagnostic;
  e::material_description material;
  std::vector<std::byte> output{std::byte{42}};
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, material),
                                           output, diagnostic) != e::error::invalid_handle ||
      output.size() != 1)
    return 1;
  if (gneiss::test_support::assign_value(gneiss::engine::meta::default_registry(), registry)
          .failed() ||
      e::register_reflection().failed() || e::register_builtin_resource_types().failed())
    return 2;
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, material),
                                           output, diagnostic) != e::error::not_ready)
    return 3;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<nullptr, nullptr>()
          .commit() != e::error::invalid_argument)
    return 4;
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
  gneiss::engine::meta::serialization_binding binding;
  if (gneiss::test_support::assign_value(
          gneiss::engine::meta::find_serialization(registry, e::runtime_type_of<probe>()), binding)
          .failed() ||
      !binding.encode || !binding.decode)
    return 7;
  probe object;
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, object),
                                           output, diagnostic) != e::error::out_of_memory ||
      output.size() != 1 || output[0] != std::byte{42})
    return 8;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(object)>>(registry,
                                                                                   bytes("bad")),
          object, diagnostic) != e::error::invalid_argument ||
      object.number != 3)
    return 9;
  material.parameters.color = {0.25F, 0.5F, 0.75F, 1};
  material.parameters.metallic = 0.5F;
  material.parameters.alpha_mode = e::material_alpha_mode::mask;
  material.parameters.double_sided = true;
  material.texture_uris[0] = "asset://textures/纹理\".json";
  material.parameters.sampling[0].uv_set = 1;
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, material),
                                           output, diagnostic)
          .failed())
    return 10;
  e::material_description restored;
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                     output),
          restored, diagnostic)
          .failed() ||
      restored.parameters.color != material.parameters.color ||
      restored.texture_uris != material.texture_uris || restored.parameters.metallic != 0.5F ||
      restored.parameters.alpha_mode != e::material_alpha_mode::mask ||
      !restored.parameters.double_sided || restored.parameters.sampling[0].uv_set != 1)
    return 11;
  const auto saved = output;
  material.parameters.color[0] = std::numeric_limits<float>::infinity();
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, material),
                                           output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 12;
  material.parameters.color[0] = 2;
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(registry, material),
                                           output, diagnostic) != e::error::invalid_argument ||
      output != saved)
    return 13;
  for (unsigned version : {1U, 2U, 3U, 4U, 5U, 7U}) {
    const auto text = std::string{R"({"format":"gneiss.material","version":)"} +
                      std::to_string(version) + R"(,"color":[1,1,1,1]})";
    if (gneiss::test_support::assign_decoded(
            gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                       bytes(text)),
            restored, diagnostic) != e::error::unsupported ||
        restored.parameters.color[0] != 0.25F)
      return 14;
  }
  if (gneiss::test_support::assign_decoded(
          gneiss::engine::meta::deserialize<std::remove_cvref_t<decltype(restored)>>(registry,
                                                                                     bytes("{")),
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
  if (gneiss::test_support::assign_decoded(gneiss::engine::meta::serialize(no_codec, object),
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
  auto owned = e::meta::deserialize<move_only>(*owned_registry, bytes("A"));
  if (!owned || !owned->number || *owned->number != 65)
    return 20;
  auto rejected = e::meta::deserialize<move_only>(*owned_registry, {});
  if (rejected || rejected.error().error != e::error::invalid_argument ||
      rejected.error().byte_offset != 3 || rejected.error().path != "/number")
    return 21;
  // 反射创建的拥有者可共享/擦除为 const；借用视图不承担所有权。
  auto shared_object =
      e::meta::deserialize_object(*owned_registry, e::runtime_type_of<move_only>(), bytes("A"));
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
  auto decoder = e::detail::make_reflected_decoder<move_only>();
  if (!decoder)
    return 22;
  auto decoded = (*decoder)->decode({std::byte{42}});
  if (!decoded || *std::static_pointer_cast<const move_only>(*decoded)->number != 42)
    return 23;
  return 0;
}
