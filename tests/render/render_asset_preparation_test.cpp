// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_asset_preparation.hpp"

#include "engine/asset/virtual_file_system.hpp"

#include <cstring>
#include <limits>
#include <thread>

namespace {
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
    output.clear();
    if (path != "mesh") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (mesh.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      output.resize(mesh.size());
      std::memcpy(output.data(), mesh.data(), mesh.size());
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
  using namespace gneiss::render_internal;
  prepared_render_batch output;
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
          render_asset_reload{.uri = "asset://mesh", .type = render_asset_type::mesh},
      };
      result = prepare_render_assets(files, requests, output, diagnostic, {});
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
      render_asset_reload{.uri = "asset://mesh", .type = render_asset_type::mesh},
  };
  if (prepare_render_assets(files, requests, output, diagnostic, [] { return true; }) !=
          GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || output.bytes != 0U) {
    return 3;
  }
  if (prepare_render_assets(files, requests, output, diagnostic, {}, 1U, 1U) == GNEISS_SUCCESS ||
      !output.assets.empty()) {
    return 4;
  }
  source->change_on_verify = true;
  if (prepare_render_assets(files, requests, output, diagnostic, {}) !=
          GNEISS_ERROR_INVALID_STATE ||
      !output.assets.empty() || diagnostic.result != GNEISS_ERROR_INVALID_STATE) {
    return 5;
  }
  const std::array missing{
      render_asset_reload{.uri = "asset://missing", .type = render_asset_type::mesh},
  };
  if (prepare_render_assets(files, missing, output, diagnostic, {}) != GNEISS_ERROR_NOT_FOUND ||
      !output.assets.empty()) {
    return 6;
  }
  return 0;
} catch (...) {
  return 7;
}
