// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/loaders/material_description_decoder.hpp"
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
  auto input = bytes(text);
  check(decode_material_description(input, *old, diagnostic).ok());
  input = bytes(
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[0,1,0,1],"base_color_texture":"asset://missing"})");
  auto current = std::make_shared<material_description>();
  check(decode_material_description(input, *current, diagnostic).ok());
  prepared_asset prepared;
  const asset_preparation_source source{"asset://material.json", asset_type::material};
  check(decode_loaded_asset(source, {}, {}, {}, prepared, diagnostic, limit, {},
                            asset_ref<const material_description>{old}, 256)
            .ok());
  old.reset();
  input.clear();
  check(prepared.description && prepared.material.color[0] == 1 &&
        current->parameters.color[1] == 1);
  check(prepared.sources.empty());
  check(decode_loaded_asset(source, {}, {}, {}, prepared, diagnostic, limit, {},
                            asset_ref<const material_description>{current}, 256)
            .ok());
  check(prepared.texture_uris[0] == "asset://missing");
  const auto retained = prepared.description;
  check(decode_loaded_asset(source, {}, {}, {}, prepared, diagnostic, 1, {}, retained, 256) ==
        result::out_of_memory);
  check(prepared.description == retained && prepared.material.color[1] == 1);
  check(decode_material_description(bytes("{}"), *current, diagnostic).failed());
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
