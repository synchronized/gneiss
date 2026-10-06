// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/asset_preparation.hpp"

#include "engine/asset/virtual_file_system.hpp"

#include <cstring>
#include <limits>
#include <thread>

namespace {
// 准备契约不能再次退化为带纹理句柄与发布操作的资源结构。
template <typename T>
concept has_texture_handle = requires(T value) { value.base_color_texture; };
template <typename T>
concept has_publication_description = requires(T value) { value.description(); };
static_assert(!has_texture_handle<decltype(gneiss::asset_internal::prepared_asset::material)>);
static_assert(
    !has_publication_description<decltype(gneiss::asset_internal::prepared_asset::material)>);

template <typename T>
concept has_upload_reference = requires(T value) { value.upload_payload; };
static_assert(!has_upload_reference<decltype(gneiss::asset_internal::prepared_asset::texture)>);

class mesh_source final : public gneiss::asset_internal::file_system {
public:
  mutable unsigned reads{};
  bool change_on_verify{};
  gneiss_result read(std::string_view path,
                     std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, std::numeric_limits<std::size_t>::max(), output);
  }
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    constexpr std::string_view mesh =
        R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})";
    constexpr std::string_view material =
        R"({"format":"gneiss.material","version":1,"color":[1,1,1,1]})";
    constexpr std::string_view linked_material =
        R"({"format":"gneiss.material","version":2,"color":[1,1,1,1],"base_color_texture":"asset://dependency"})";
    auto bytes = mesh;
    if (path == "material") {
      bytes = material;
    } else if (path == "linked") {
      bytes = linked_material;
    }
    output.clear();
    if (path != "mesh" && path != "material" && path != "linked") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (bytes.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      output.resize(bytes.size());
      std::memcpy(output.data(), bytes.data(), bytes.size());
      if (++reads > 1U && change_on_verify) {
        output.back() = std::byte{' '};
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};
} // namespace

int main() try {
  using namespace gneiss::asset_internal;
  prepared_batch output;
  asset_diagnostic diagnostic;
  gneiss_result result = GNEISS_ERROR_INTERNAL;
  // 不创建 Application、缓存或资源服务；后台输出在 VFS 销毁后仍拥有完整数组。
  std::thread worker([&] {
    try {
      gneiss::asset_internal::virtual_file_system files;
      auto source = std::make_shared<mesh_source>();
      if (files.mount("asset://", source) != GNEISS_SUCCESS) {
        return;
      }
      const std::array requests{
          asset_request{.uri = "asset://mesh", .type = asset_type::mesh},
      };
      result = prepare_assets(files, requests, output, diagnostic, {}, 0U);
    } catch (...) {
      result = GNEISS_ERROR_OUT_OF_MEMORY;
    }
  });
  worker.join();
  if (result != GNEISS_SUCCESS || output.assets.size() != 1U ||
      output.assets[0].mesh.vertices.size() != 3U || output.bytes == 0U) {
    return 1;
  }

  gneiss::asset_internal::virtual_file_system files;
  auto source = std::make_shared<mesh_source>();
  if (files.mount("asset://", source) != GNEISS_SUCCESS) {
    return 2;
  }
  const std::array requests{
      asset_request{.uri = "asset://mesh", .type = asset_type::mesh},
  };
  if (prepare_assets(
          files, requests, output, diagnostic, [] { return true; }, 0U) !=
          GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || output.bytes != 0U) {
    return 3;
  }
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U, 1U, 1U) == GNEISS_SUCCESS ||
      !output.assets.empty()) {
    return 4;
  }
  source->change_on_verify = true;
  if (prepare_assets(files, requests, output, diagnostic, {}, 0U) != GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || diagnostic.result != GNEISS_ERROR_INVALID_STATE) {
    return 5;
  }
  const std::array missing{
      asset_request{.uri = "asset://missing", .type = asset_type::mesh},
  };
  if (prepare_assets(files, missing, output, diagnostic, {}, 0U) != GNEISS_ERROR_NOT_FOUND ||
      !output.assets.empty()) {
    return 6;
  }
  source->change_on_verify = false;
  const std::array material_request{
      asset_request{.uri = "asset://material", .type = asset_type::material},
  };
  if (prepare_assets(files, material_request, output, diagnostic, {}, 1000U, 1U, 1000U) !=
          GNEISS_SUCCESS ||
      output.bytes != 1000U || output.assets.size() != 1U) {
    return 8;
  }
  if (prepare_assets(files, material_request, output, diagnostic, {}, 0U) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      !output.assets.empty()) {
    return 9;
  }
  if (prepare_assets(files, material_request, output, diagnostic, {}, 1001U, 1U, 1000U) !=
          GNEISS_ERROR_OUT_OF_MEMORY ||
      !output.assets.empty()) {
    return 10;
  }
  const std::array linked_request{
      asset_request{.uri = "asset://linked", .type = asset_type::material},
  };
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  if (prepare_assets(files, linked_request, output, diagnostic, {}, maximum, 2U, maximum) !=
          GNEISS_ERROR_OUT_OF_MEMORY ||
      !output.assets.empty()) {
    return 11;
  }
  return 0;
} catch (...) {
  return 7;
}
