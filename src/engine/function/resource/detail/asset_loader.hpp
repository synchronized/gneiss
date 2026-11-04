// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <optional>
namespace gneiss::engine::detail {
/** 一次加载的格式状态；管理器负责 I/O 和调度，Loader 只消费字节并描述下一次读取。 */
struct asset_load_task {
  std::string uri, next_uri;
  std::uint32_t source_type{};
  std::shared_ptr<const void> value;
  asset_load_metrics metrics;
  meta::serialization_diagnostic diagnostic;
  error status{error::not_ready};
  explicit asset_load_task(std::string path) : uri(std::move(path)), next_uri(uri) {}
  virtual ~asset_load_task() = default;
  virtual void decode(const std::vector<std::byte>& bytes) = 0;
  std::size_t input_bytes{};
  void reject(asset_diagnostic failure) {
    status = failure.error;
    diagnostic = {failure.byte_offset, std::move(failure.path), std::move(failure.message), status};
  }
};
struct object_load_task final : asset_load_task {
  std::shared_ptr<const asset_decoder> decoder;
  object_load_task(std::string uri, std::shared_ptr<const asset_decoder> parser)
      : asset_load_task(std::move(uri)), decoder(std::move(parser)) {}
  void decode(const std::vector<std::byte>& bytes) override {
    auto result = decoder->decode(bytes);
    status = result ? error::success : result.error().error;
    if (result)
      value = std::move(*result);
    else
      diagnostic = std::move(result).error();
    metrics.bytes = input_bytes;
  }
};
struct mesh_load_task final : asset_load_task {
  explicit mesh_load_task(std::string uri) : asset_load_task(std::move(uri)) {
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
struct texture_load_task final : asset_load_task {
  texture_prepare_profile profile;
  std::optional<texture_description> description;
  texture_load_task(std::string uri, const asset_load_options& options)
      : asset_load_task(std::move(uri)), profile(options.texture_profile) {
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
template <class Resolve>
result<std::shared_ptr<asset_load_task>>
make_asset_load_task(type_id type, std::string uri, asset_load_options options, Resolve resolve) {
  if (type == runtime_type_of<mesh_data>())
    return std::shared_ptr<asset_load_task>{std::make_shared<mesh_load_task>(std::move(uri))};
  if (type == runtime_type_of<texture_data>())
    return std::shared_ptr<asset_load_task>{
        std::make_shared<texture_load_task>(std::move(uri), options)};
  auto decoder = resolve();
  if (!decoder)
    return decoder.error();
  return std::shared_ptr<asset_load_task>{
      std::make_shared<object_load_task>(std::move(uri), std::move(*decoder))};
}
}
