// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/loader_factory.hpp"
namespace gneiss::engine::detail {
namespace {
struct object_loader final : asset_loader {
  std::shared_ptr<const asset_decoder> decoder;
  object_loader(std::string uri, std::shared_ptr<const asset_decoder> parser)
      : asset_loader(std::move(uri)), decoder(std::move(parser)) {}
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

}
std::shared_ptr<asset_loader> make_object_loader(std::string uri,
                                                 std::shared_ptr<const asset_decoder> decoder) {
  return std::make_shared<object_loader>(std::move(uri), std::move(decoder));
}
}
