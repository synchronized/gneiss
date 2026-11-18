// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include "function/asset/detail/asset_decode.hpp"
#include <cstdio>
#include <cstring>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <stdexcept>
#include <type_traits>
using namespace gneiss;
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
  auto material = gneiss::asset_file::deserialize<material_description>(input);
  check(material.ok());
  auto root = meta::view(*material);
  auto parameters = root.template capability<gneiss::meta::cap::object>()->member("parameters");
  check(parameters.ok());
  auto color = parameters->template capability<gneiss::meta::cap::object>()->member("color");
  check(color && color->read_value()->kind() == value_kind::vec4);
  check(color->assign(value{math::vector4{0.2F, 0.4F, 0.6F, 1}}).ok());
  auto sampling = parameters->template capability<gneiss::meta::cap::object>()
                      ->member("sampling")
                      ->as_sequence();
  check(sampling && !sampling->is_resizable() && sampling->size() == 5);
  check(sampling->resize(4) == error::invalid_argument && sampling->resize(5).ok());
  auto uv = sampling->at(0)->template capability<gneiss::meta::cap::object>()->member("uv_set");
  check(uv.ok());
  check(uv->assign(value{std::uint64_t{1} << 32}) == error::invalid_argument);
  check(material->parameters.sampling[0].uv_set == 0);
  auto encoded = gneiss::asset_file::serialize_bytes(*material);
  check(encoded.ok());
  auto restored = gneiss::asset_file::deserialize<material_description>(*encoded);
  check(restored && restored->parameters.color == material->parameters.color);
  // 同一资源描述作为普通对象或嵌套元素时不携带文件头。
  auto plain = serialization::json::encode(*material);
  check(plain && plain->find("\"format\"") == std::string::npos &&
        plain->find("\"version\"") == std::string::npos);
  auto plain_copy = serialization::json::decode<material_description>(std::string_view{*plain});
  check(plain_copy && plain_copy->parameters.color == material->parameters.color);
  check(!asset_file::deserialize<material_description>(std::string_view{*plain}));
  auto nested = serialization::json::encode(std::vector<material_description>{*material});
  check(nested && nested->find("\"format\"") == std::string::npos);
  auto nested_copy =
      serialization::json::decode<std::vector<material_description>>(std::string_view{*nested});
  check(nested_copy && nested_copy->size() == 1 &&
        nested_copy->front().parameters.color == material->parameters.color);
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
    check(!gneiss::asset_file::deserialize<material_description>(bytes(malformed)));
  for (
      const auto malformed :
      {R"({"format":"gneiss.material","format":"gneiss.material","version":6,"parameters":{},"texture_uris":["","","","",""]})",
       R"({"format":"gneiss.material","version":6,"version":6,"parameters":{},"texture_uris":["","","","",""]})",
       R"({"format":"other","version":6})", R"({"format":"gneiss.material","version":0})",
       R"({"format":"gneiss.material","version":"6"})", R"([])"})
    check(!gneiss::asset_file::deserialize<material_description>(bytes(malformed)));
  auto obsolete = gneiss::asset_file::deserialize<material_description>(
      bytes(R"({"format":"gneiss.material","version":5})"));
  check(!obsolete && obsolete.error().error == error::unsupported &&
        obsolete.error().path == "/version");
  auto length_error = gneiss::asset_file::deserialize<material_description>(
      bytes(R"({"format":"gneiss.material","version":6,"parameters":{},"texture_uris":[]})"));
  check(!length_error && length_error.error().path == "/texture_uris");
}
void run(std::size_t limit) {
  std::string text =
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
  text.resize(limit, ' ');
  auto old = std::make_shared<material_description>();
  asset_diagnostic diagnostic;
  serialization::diagnostic codec_diagnostic;
  auto input = bytes(text);
  check(gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<material_description>(input), *old,
            codec_diagnostic)
            .ok());
  input = bytes(
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["asset://missing","","","",""]})");
  auto current = std::make_shared<material_description>();
  check(gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<material_description>(input), *current,
            codec_diagnostic)
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
  check(gneiss::test_support::assign_decoded(
            gneiss::asset_file::deserialize<material_description>(bytes("{}")), *current,
            codec_diagnostic)
            .failed());
  check(current->parameters.color[1] == 1 && current->texture_uris[0] == "asset://missing");
}
}
int main() try {
  check(gneiss::serialization::register_builtin_types().ok());
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
