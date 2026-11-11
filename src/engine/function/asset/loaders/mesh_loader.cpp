// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/function/asset/detail/asset_decode.hpp"
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/function/render/service.hpp"
namespace gneiss::detail {
namespace {
struct mesh_loader final : asset_loader {
  explicit mesh_loader(std::string uri) : asset_loader(std::move(uri)) {
    source_type = static_cast<std::uint32_t>(render_resource_kind::mesh);
  }
  void decode(const std::vector<std::byte>& bytes) override {
    auto result = decode_mesh_asset(uri, bytes);
    if (!result) {
      reject(std::move(result).error());
      return;
    }
    metrics.bytes = result->bytes;
    value = std::make_shared<const mesh_data>(std::move(std::get<mesh_data>(result->content)));
    status = error::success;
  }
};

}
std::shared_ptr<asset_loader> make_mesh_loader(std::string uri) {
  return std::make_shared<mesh_loader>(std::move(uri));
}
// 所属线程采用内容；工作线程只调用上方的字节解码入口。
error asset_construction::adopt(asset_manager& assets, std::shared_ptr<const mesh_data> content,
                                const ref<const gneiss::mesh>& target,
                                std::span<const asset_read_record> sources) {
  if (!assets.impl_ || assets.stop_requested())
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  auto prepared = assets.impl_->render().prepare_mesh_data(std::move(content));
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  auto status = assets.impl_->update_mesh(target, std::move(data));
  return status ? error::success : status.error();
}
}
