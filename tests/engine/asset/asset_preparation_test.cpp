// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include "function/asset/detail/asset_decode.hpp"
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/mesh/mesh_description.hpp>
#include <gneiss/engine/resource/texture/texture_decoder.hpp>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace gneiss;
namespace {
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("资产内存解码契约失败");
}
std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> value(text.size());
  std::memcpy(value.data(), text.data(), text.size());
  return value;
}
}
int main() try {
  check(initialize_meta().ok());
  const render_resource_source mesh{"asset://mesh", render_resource_kind::mesh};
  constexpr auto text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
  decoded_asset output;
  asset_diagnostic diagnostic;
  error status;
  // 后台只消费字节，输入销毁后输出仍拥有网格数组。
  std::thread worker([&] {
    status = gneiss::test_support::assign_decoded(decode_mesh_asset(mesh.uri, bytes(text)), output,
                                                  diagnostic);
  });
  worker.join();
  check(status.ok() && std::get<mesh_data>(output.content).vertices.size() == 3 &&
        output.bytes > 0);
  const auto* vertices = std::get<mesh_data>(output.content).vertices.data();
  check(std::get<mesh_data>(output.content).normals.size() == 3 &&
        std::get<mesh_data>(output.content).normals[0].z == 1.0F);
  // 版本拒绝不能覆盖上一次成功结果；旧布局无需补齐 v3 字段也应报告版本错误。
  for (const auto version : {1, 2, 4}) {
    const auto legacy = std::string{"{\"format\":\"gneiss.mesh\",\"version\":"} +
                        std::to_string(version) +
                        R"(,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    check(gneiss::test_support::assign_decoded(decode_mesh_asset(mesh.uri, bytes(legacy)), output,
                                               diagnostic) == error::unsupported);
    check(diagnostic.path == "/version" &&
          std::get<mesh_data>(output.content).vertices.data() == vertices);
  }
  for (const auto field : {"uvs", "normals"}) {
    std::string missing{text};
    const auto begin = missing.find(std::string{",\""} + field + "\":[");
    const auto end = missing.find("]]", begin) + 2;
    missing.erase(begin, end - begin);
    check(gneiss::test_support::assign_decoded(decode_mesh_asset(mesh.uri, bytes(missing)), output,
                                               diagnostic) == error::invalid_argument);
    check(diagnostic.path == std::string{"/"} + field &&
          std::get<mesh_data>(output.content).vertices.data() == vertices);
  }
  check(gneiss::test_support::assign_decoded(decode_mesh_asset(mesh.uri, bytes("{}")), output,
                                             diagnostic)
            .failed());
  check(std::get<mesh_data>(output.content).vertices.data() == vertices);
  // 反射读取负责形状和数值；业务校验负责数组对应与法线归一化。
  for (
      const auto malformed :
      {R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[],"normals":[[0,0,1],[0,0,1],[0,0,1]]})",
       R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,2],[0,0,1],[0,0,1]]})",
       R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})"}) {
    check(gneiss::test_support::assign_decoded(decode_mesh_asset(mesh.uri, bytes(malformed)),
                                               output, diagnostic) == error::invalid_argument);
    check(std::get<mesh_data>(output.content).vertices.data() == vertices);
  }
  check(decoded_asset{}.kind() == render_resource_kind::invalid);
  const render_resource_source material{"asset://material", render_resource_kind::material};
  auto description = std::make_shared<material_description>();
  asset_ref<const material_description> reference{description};
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, reference, 1000),
                                             output, diagnostic)
            .ok());
  check(std::get<asset_ref<const material_description>>(output.content) == reference &&
        output.bytes == 1000);
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, reference, 0),
                                             output, diagnostic) == error::invalid_argument);
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, reference, 1001),
                                             output, diagnostic)
            .ok());
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, {}, 1000), output,
                                             diagnostic) == error::invalid_argument);
  description->texture_uris[0] = "asset://texture";
  const auto maximum = std::numeric_limits<std::size_t>::max();
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, reference, maximum),
                                             output, diagnostic) == error::out_of_memory);
  check(gneiss::test_support::assign_decoded(adopt_material_asset(material.uri, reference, 1000),
                                             output, diagnostic)
            .ok());
  check(std::get<asset_ref<const material_description>>(output.content)->texture_uris[0] ==
        "asset://texture");
  // 解码不递归加载依赖；不存在的纹理只作为 URI 返回给加载编排层。
  const auto texture = bytes(
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.png","color_space":"srgb","usage":"default"})");
  texture_description parsed;
  check(gneiss::test_support::assign_decoded(detail::parse_texture(texture), parsed, diagnostic)
            .ok());
  check(gneiss::test_support::assign_decoded(
            decode_texture_asset("asset://texture", parsed, {}, {}), output, diagnostic)
            .failed());
  check(output.kind() == render_resource_kind::material);
  for (const auto version : {1, 3}) {
    const auto source_text = std::string{R"({"format":"gneiss.texture","version":)"} +
                             std::to_string(version) +
                             R"(,"source":"asset://a.png","color_space":"srgb","usage":"default"})";
    check(gneiss::test_support::assign_decoded(detail::parse_texture(bytes(source_text)), parsed,
                                               diagnostic) == error::unsupported);
    check(diagnostic.path == "/version");
  }
  for (const auto usage : {std::string_view{}, std::string_view{",\"usage\":\"unknown\""},
                           std::string_view{",\"usage\":\"normal\""}}) {
    diagnostic = {};
    const auto source_text =
        std::string{
            R"({"format":"gneiss.texture","version":2,"source":"asset://a.png","color_space":"srgb")"} +
        std::string{usage} + "}";
    check(gneiss::test_support::assign_decoded(detail::parse_texture(bytes(source_text)), parsed,
                                               diagnostic) == error::invalid_argument);
    check(diagnostic.path == "/usage");
  }
  // 通用序列化保留结构；文件与负载采用边界执行领域约束。
  mesh_description invalid_mesh;
  invalid_mesh.topology = "triangle_list";
  auto mesh_content = serialization::json::encode_bytes(invalid_mesh);
  check(bool(mesh_content));
  check(bool(serialization::json::decode<mesh_description>(*mesh_content)));
  check(!asset_file::serialize_bytes(invalid_mesh));
  auto invalid_mesh_file = asset_file::deserialize<mesh_description>(bytes(
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[],"uvs":[],"normals":[]})"));
  check(!invalid_mesh_file && invalid_mesh_file.error().path == "/vertices");
  texture_description invalid_texture{.uri = "asset://image.png",
                                      .color_space = texture_color_space::srgb,
                                      .usage = texture_usage::normal};
  auto texture_content = serialization::json::encode_bytes(invalid_texture);
  check(bool(texture_content));
  check(bool(serialization::json::decode<texture_description>(*texture_content)));
  check(!asset_file::serialize_bytes(invalid_texture));
  auto payload = detail::decode_texture_payload(invalid_texture, {}, {});
  check(!payload && payload.error().path == "/usage");
  invalid_texture.color_space = static_cast<texture_color_space>(99);
  check(validate_texture_description(invalid_texture).path == "/color_space");
  material_description invalid_material;
  invalid_material.parameters.roughness = 2.0F;
  auto material_content = serialization::json::encode_bytes(invalid_material);
  check(bool(material_content));
  check(bool(serialization::json::decode<material_description>(*material_content)));
  auto material_file = asset_file::serialize_bytes(invalid_material);
  check(!material_file && material_file.error().path == "/parameters/roughness");
  invalid_material.parameters.roughness = 0.5F;
  invalid_material.parameters.sampling[2].uv_set = 2;
  auto invalid_adoption =
      adopt_material_asset("asset://bad", make_ref<material_description>(invalid_material), 1);
  check(!invalid_adoption && invalid_adoption.error().path == "/parameters/sampling/2");
  return 0;
} catch (...) {
  return 1;
}
