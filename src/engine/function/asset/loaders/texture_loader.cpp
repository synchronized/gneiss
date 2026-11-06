// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/function/asset/detail/asset_decode.hpp"
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <optional>
namespace gneiss::engine::detail {
namespace {
struct texture_loader final : asset_loader {
  texture_prepare_profile profile;
  std::optional<texture_description> description;
  texture_loader(std::string uri, const asset_load_options& options)
      : asset_loader(std::move(uri)), profile(options.texture_profile) {
    source_type = static_cast<std::uint32_t>(render_resource_kind::texture);
  }
  void decode(const std::vector<std::byte>& bytes) override {
    if (!description) {
      auto result = parse_texture(bytes);
      if (!result) {
        reject(std::move(result).error());
        return;
      }
      description = std::move(*result);
      next_uri = description->uri;
      status = error::success;
      return;
    }
    auto result = decode_texture_asset(uri, *description, bytes, profile);
    if (!result) {
      reject(std::move(result).error());
      return;
    }
    metrics.bytes = result->bytes;
    value =
        std::make_shared<const texture_data>(std::move(std::get<texture_data>(result->content)));
    status = error::success;
  }
};

}
std::shared_ptr<asset_loader> make_texture_loader(std::string uri,
                                                  const asset_load_options& options) {
  return std::make_shared<texture_loader>(std::move(uri), options);
}
// 所属线程采用内容；工作线程只调用上方的字节解码入口。
error asset_construction::adopt(asset_manager& assets, std::shared_ptr<const texture_data> content,
                                const texture& target,
                                std::span<const asset_read_record> sources) noexcept try {
  if (!assets.impl_ || assets.stop_requested())
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  auto prepared = assets.impl_->render().prepare_texture_data(std::move(content));
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  return assets.impl_->update_texture(target, std::move(data)) ? error::success
                                                               : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

}
