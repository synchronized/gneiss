// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include "engine/core/meta/instance.hpp"
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
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
void native_material_contract() {
  auto input = bytes(
      R"({"format":"gneiss.material","version":6,"parameters":{},"texture_uris":["","","","",""]})");
  auto material = decode_material(input);
  check(material.ok());
  auto root = meta::mutable_instance::from(*material);
  check(root.ok());
  auto parameters = root->property("parameters");
  check(parameters.ok());
  auto color = parameters->property("color");
  check(color && color->read_value()->kind() == value_kind::vec4);
  check(color->write_value(value{math::vector4{0.2F, 0.4F, 0.6F, 1}}).ok());
  auto sampling = parameters->property("sampling")->as_sequence();
  check(sampling && !sampling->is_resizable() && *sampling->size() == 5);
  check(sampling->resize(4) == error::invalid_argument && sampling->resize(5).ok());
  auto uv = sampling->at(0)->property("uv_set");
  check(uv.ok());
  check(uv->write_value(value{std::uint64_t{1} << 32}) == error::invalid_argument);
  check(material->parameters.sampling[0].uv_set == 0);
  auto encoded = encode_material(*material);
  check(encoded.ok());
  auto restored = decode_material(*encoded);
  check(restored && restored->parameters.color == material->parameters.color);
  const std::string text(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  check(text.find("\"parameters\"") != std::string::npos &&
        text.find("base_color_texture") == std::string::npos);
  for (
      const auto malformed :
      {R"({"format":"gneiss.material","version":5,"parameters":{},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{},"texture_uris":[""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"sampling":[{}]},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"alpha_mode":3},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"alpha_mode":4294967296},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"sampling":[{"uv_set":4294967296},{},{},{},{}]},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"color":[1,1,1]},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{"unknown":1},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"parameters":{},"parameters":{},"texture_uris":["","","","",""]})"})
    check(!decode_material(bytes(malformed)));
  for (
      const auto malformed :
      {R"({"format":"gneiss.material","format":"gneiss.material","version":6,"parameters":{},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"version":6,"parameters":{},"texture_uris":["","","","",""]})",
       R"({"format":"other","version":6})", R"({"format":"gneiss.material","version":0})",
       R"({"format":"gneiss.material","version":"6"})", R"([])"})
    check(!decode_material(bytes(malformed)));
  auto obsolete = decode_material(bytes(R"({"format":"gneiss.material","version":5})"));
  check(!obsolete && obsolete.error().error == error::unsupported &&
        obsolete.error().path == "/version");
  auto length_error = decode_material(
      bytes(R"({"format":"gneiss.material","version":6,"parameters":{},"texture_uris":[]})"));
  check(!length_error && length_error.error().path == "/texture_uris");
}
void run(std::size_t limit) {
  std::string text =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
  text.resize(limit, ' ');
  auto old = std::make_shared<material_description>();
  asset_diagnostic diagnostic;
  meta::serialization_diagnostic codec_diagnostic;
  auto input = bytes(text);
  check(gneiss::test_support::assign_decoded(decode_material(input), *old, codec_diagnostic).ok());
  input = bytes(
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["asset://missing","","","",""]})");
  auto current = std::make_shared<material_description>();
  check(gneiss::test_support::assign_decoded(decode_material(input), *current, codec_diagnostic)
            .ok());
  decoded_asset prepared;
  const render_resource_source source{"asset://material.json", render_resource_kind::material};
  check(gneiss::test_support::assign_decoded(
            adopt_material_asset(source.uri, asset_ref<const material_description>{old}, 256),
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
            adopt_material_asset(source.uri, asset_ref<const material_description>{current}, 256),
            prepared, diagnostic)
            .ok());
  check(std::get<asset_ref<const material_description>>(prepared.content)->texture_uris[0] ==
        "asset://missing");
  const auto retained = std::get<asset_ref<const material_description>>(prepared.content);
  check(gneiss::test_support::assign_decoded(adopt_material_asset(source.uri, {}, 256), prepared,
                                             diagnostic) == error::invalid_argument);
  check(std::get<asset_ref<const material_description>>(prepared.content) == retained &&
        std::get<asset_ref<const material_description>>(prepared.content)->parameters.color[1] ==
            1);
  check(
      gneiss::test_support::assign_decoded(decode_material(bytes("{}")), *current, codec_diagnostic)
          .failed());
  check(current->parameters.color[1] == 1 && current->texture_uris[0] == "asset://missing");
}
}
int main() try {
  check(register_reflection().ok());
  check(register_builtin_resource_types().ok());
  check(meta::freeze().ok());
  native_material_contract();
  run(1024U * 1024U);
  run(2U * 1024U * 1024U);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
