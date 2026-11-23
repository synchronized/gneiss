// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/asset/asset_reference_context.hpp>
#include "function/asset/detail/loader_factory.hpp"
namespace gneiss::detail {
namespace {
struct object_loader final : asset_loader {
  std::shared_ptr<const asset_decoder> decoder;
  object_loader(std::string uri, std::shared_ptr<const asset_decoder> parser)
      : asset_loader(std::move(uri)), decoder(std::move(parser)) {}
  asset_reference_context references;
  std::vector<std::byte> deferred_bytes;
  void decode(const std::vector<std::byte>& bytes) override {
    diagnostic = references.collect([&](const serialization::read_context& context) {
      auto decoded = decoder->decode(context, bytes);
      if (decoded)
        value = *decoded;
      return decoded;
    });
    status = diagnostic.error;
    if (status.ok() && !references.slots().empty()) {
      dependencies = references.slots();
      deferred_bytes = bytes;
    }
    metrics.bytes = input_bytes;
  }
  error finish() override {
    if (deferred_bytes.empty())
      return error::success;
    serialization::read_context context{references};
    auto decoded = decoder->decode(context, deferred_bytes);
    if (!decoded) {
      diagnostic = std::move(decoded).error();
      return diagnostic.error;
    }
    value = std::move(*decoded);
    deferred_bytes.clear();
    return error::success;
  }
};

}
std::shared_ptr<asset_loader> make_object_loader(std::string uri,
                                                 std::shared_ptr<const asset_decoder> decoder) {
  return std::make_shared<object_loader>(std::move(uri), std::move(decoder));
}
}
