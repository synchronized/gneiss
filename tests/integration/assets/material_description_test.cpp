// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/resource/material/material_codec.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
using namespace gneiss::engine;
static_assert(!std::is_polymorphic_v<material_description>);
namespace {
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("材质描述解码契约失败");
}
std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> value(text.size());
  std::memcpy(value.data(), text.data(), text.size());
  return value;
}
void run(std::size_t limit) {
  std::string text =
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"base_color_texture":null,"color":[1,0,0,1]})";
  text.resize(limit, ' ');
  auto old = std::make_shared<material_description>();
  asset_diagnostic diagnostic;
  meta::serialization_diagnostic codec_diagnostic;
  auto input = bytes(text);
  check(decode_material(input, *old, codec_diagnostic).ok());
  input = bytes(
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[0,1,0,1],"base_color_texture":"asset://missing"})");
  auto current = std::make_shared<material_description>();
  check(decode_material(input, *current, codec_diagnostic).ok());
  decoded_asset prepared;
  const asset_source source{"asset://material.json", asset_type::material};
  check(
      gneiss::test_support::assign_decoded(
          adopt_material_asset(source.uri, asset_ref<const material_description>{old}, limit, 256),
          prepared, diagnostic)
          .ok());
  old.reset();
  input.clear();
  check(std::get<asset_ref<const material_description>>(prepared.content) &&
        std::get<asset_ref<const material_description>>(prepared.content)->parameters.color[0] ==
            1 &&
        current->parameters.color[1] == 1);
  check(prepared.sources.empty());
  check(gneiss::test_support::assign_decoded(
            adopt_material_asset(source.uri, asset_ref<const material_description>{current}, limit,
                                 256),
            prepared, diagnostic)
            .ok());
  check(std::get<asset_ref<const material_description>>(prepared.content)->texture_uris[0] ==
        "asset://missing");
  const auto retained = std::get<asset_ref<const material_description>>(prepared.content);
  check(gneiss::test_support::assign_decoded(adopt_material_asset(source.uri, retained, 1, 256),
                                             prepared, diagnostic) == error::out_of_memory);
  check(std::get<asset_ref<const material_description>>(prepared.content) == retained &&
        std::get<asset_ref<const material_description>>(prepared.content)->parameters.color[1] ==
            1);
  check(decode_material(bytes("{}"), *current, codec_diagnostic).failed());
  check(current->parameters.color[1] == 1 && current->texture_uris[0] == "asset://missing");
}
}
int main() try {
  run(1024U * 1024U);
  run(2U * 1024U * 1024U);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
