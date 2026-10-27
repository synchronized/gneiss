// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
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
e::result bad_encode(const probe&, std::vector<std::byte>& output,
                     gneiss::engine::meta::serialization_diagnostic&) {
  output.clear();
  throw std::bad_alloc{};
}
e::result bad_decode(std::span<const std::byte>, probe& output,
                     gneiss::engine::meta::serialization_diagnostic&) {
  output.number = 9;
  return e::result::invalid_argument;
}
int main() {
  gneiss::engine::meta::registry_ref registry;
  gneiss::engine::meta::serialization_diagnostic diagnostic;
  e::material_description material;
  std::vector<std::byte> output{std::byte{42}};
  if (gneiss::engine::meta::serialize(registry, material, output, diagnostic) !=
          e::result::invalid_handle ||
      output.size() != 1)
    return 1;
  if (gneiss::engine::meta::create_registry(registry).failed() ||
      e::register_material_reflection(registry).failed())
    return 2;
  if (gneiss::engine::meta::serialize(registry, material, output, diagnostic) !=
      e::result::not_ready)
    return 3;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<nullptr, nullptr>()
          .commit() != e::result::invalid_argument)
    return 4;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<&bad_encode, &bad_decode>()
          .serialization<&bad_encode, &bad_decode>()
          .commit() != e::result::invalid_argument)
    return 5;
  if (gneiss::engine::meta::type_builder<probe>{registry}
          .serialization<&bad_encode, &bad_decode>()
          .commit()
          .failed())
    return 5;
  if (e::register_material_reflection(registry) != e::result::invalid_argument ||
      gneiss::engine::meta::freeze(registry).failed())
    return 6;
  gneiss::engine::meta::serialization_binding binding;
  if (gneiss::engine::meta::find_serialization(registry, e::runtime_type_of<probe>(), binding)
          .failed() ||
      !binding.encode || !binding.decode)
    return 7;
  probe object;
  if (gneiss::engine::meta::serialize(registry, object, output, diagnostic) !=
          e::result::out_of_memory ||
      output.size() != 1 || output[0] != std::byte{42})
    return 8;
  if (gneiss::engine::meta::deserialize(registry, bytes("bad"), object, diagnostic) !=
          e::result::invalid_argument ||
      object.number != 3)
    return 9;
  material.parameters.color = {0.25F, 0.5F, 0.75F, 1};
  material.parameters.metallic = 0.5F;
  material.parameters.alpha_mode = e::material_alpha_mode::mask;
  material.parameters.double_sided = true;
  material.texture_uris[0] = "asset://textures/纹理\".json";
  material.parameters.sampling[0].uv_set = 1;
  if (gneiss::engine::meta::serialize(registry, material, output, diagnostic).failed())
    return 10;
  e::material_description restored;
  if (gneiss::engine::meta::deserialize(registry, output, restored, diagnostic).failed() ||
      restored.parameters.color != material.parameters.color ||
      restored.texture_uris != material.texture_uris || restored.parameters.metallic != 0.5F ||
      restored.parameters.alpha_mode != e::material_alpha_mode::mask ||
      !restored.parameters.double_sided || restored.parameters.sampling[0].uv_set != 1)
    return 11;
  const auto saved = output;
  material.parameters.color[0] = std::numeric_limits<float>::infinity();
  if (gneiss::engine::meta::serialize(registry, material, output, diagnostic) !=
          e::result::invalid_argument ||
      output != saved)
    return 12;
  material.parameters.color[0] = 2;
  if (gneiss::engine::meta::serialize(registry, material, output, diagnostic) !=
          e::result::invalid_argument ||
      output != saved)
    return 13;
  for (unsigned version : {1U, 2U, 3U, 4U, 6U}) {
    const auto text = std::string{R"({"format":"gneiss.material","version":)"} +
                      std::to_string(version) + R"(,"color":[1,1,1,1]})";
    if (gneiss::engine::meta::deserialize(registry, bytes(text), restored, diagnostic) !=
            e::result::unsupported ||
        restored.parameters.color[0] != 0.25F)
      return 14;
  }
  if (gneiss::engine::meta::deserialize(registry, bytes("{"), restored, diagnostic) !=
          e::result::invalid_argument ||
      restored.parameters.color[0] != 0.25F)
    return 15;
  if (e::register_material_reflection(registry) != e::result::invalid_state)
    return 16;
  gneiss::engine::meta::registry_ref no_codec;
  if (gneiss::engine::meta::create_registry(no_codec).failed() ||
      gneiss::engine::meta::type_builder<probe>{no_codec}.commit().failed() ||
      gneiss::engine::meta::freeze(no_codec).failed())
    return 17;
  if (gneiss::engine::meta::serialize(no_codec, object, output, diagnostic) !=
          e::result::unsupported ||
      output != saved)
    return 18;
  return 0;
}
