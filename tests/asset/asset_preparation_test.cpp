// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/resource/types/material_description.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace gneiss::engine;
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
  const asset_preparation_source mesh{"asset://mesh", asset_type::mesh};
  constexpr auto text =
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
  prepared_asset output;
  asset_diagnostic diagnostic;
  result status;
  // 后台只消费字节，输入销毁后输出仍拥有网格数组。
  std::thread worker([&] {
    status = decode_loaded_asset(mesh, bytes(text), {}, {}, output, diagnostic, 65536, {});
  });
  worker.join();
  check(status.ok() && output.mesh.vertices.size() == 3 && output.bytes > 0);
  const auto* vertices = output.mesh.vertices.data();
  check(decode_loaded_asset(mesh, bytes("{}"), {}, {}, output, diagnostic, 65536, {}).failed());
  check(output.mesh.vertices.data() == vertices);
  check(decode_loaded_asset(mesh, bytes(text), {}, {}, output, diagnostic, 1, {}) ==
        result::out_of_memory);
  check(output.mesh.vertices.data() == vertices);
  check(decode_loaded_asset({"asset://invalid", asset_type::invalid}, {}, {}, {}, output,
                            diagnostic, 65536, {}) == result::invalid_argument);
  const asset_preparation_source material{"asset://material", asset_type::material};
  auto description = std::make_shared<material_description>();
  asset_ref<const material_description> reference{description};
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, 1000, {}, reference, 1000)
            .ok());
  check(output.description == reference && output.bytes == 1000);
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, 1000, {}, reference, 0) ==
        result::invalid_argument);
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, 1000, {}, reference, 1001) ==
        result::out_of_memory);
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, 1000, {}) ==
        result::unsupported);
  description->texture_uris[0] = "asset://texture";
  const auto maximum = std::numeric_limits<std::size_t>::max();
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, maximum, {}, reference,
                            maximum) == result::out_of_memory);
  check(decode_loaded_asset(material, {}, {}, {}, output, diagnostic, 65536, {}, reference, 1000)
            .ok());
  check(output.texture_uris[0] == "asset://texture");
  // 解码不递归加载依赖；不存在的纹理只作为 URI 返回给加载编排层。
  const auto texture = bytes(
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.png","color_space":"srgb"})");
  check(decode_loaded_asset({"asset://texture", asset_type::texture}, texture, "asset://wrong.png",
                            {}, output, diagnostic, 65536, {}) == result::invalid_argument);
  check(diagnostic.path == "/source");
  return 0;
} catch (...) {
  return 1;
}
