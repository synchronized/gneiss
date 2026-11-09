// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/loader_factory.hpp"
namespace gneiss::engine::detail {
/** 在工作线程构造局部内容，在所属线程采用成功结果。 */
struct action_map_loader final : asset_loader {
  asset_identity source;
  std::shared_ptr<const asset_decoder> decoder;
  action_map_loader(asset_identity identity, std::shared_ptr<const asset_decoder> parser)
      : asset_loader(identity.uri), source(std::move(identity)), decoder(std::move(parser)) {}
  std::shared_ptr<const asset> reserve() override {
    return std::shared_ptr<const action_map>(new action_map(source, {}));
  }
  void decode(const std::vector<std::byte>& bytes) override {
    auto decoded = decoder->decode(bytes);
    status = decoded ? error::success : decoded.error().error;
    if (decoded) {
      auto content = std::static_pointer_cast<const action_map_description>(std::move(*decoded));
      if (!content) {
        status = error::internal;
        return;
      }
      diagnostic = validate_action_map(*content);
      if (diagnostic.error.failed()) {
        status = diagnostic.error;
        return;
      }
      value = std::shared_ptr<const action_map>(
          new action_map(source, ref<const action_map_description>{std::move(content)}));
    } else
      diagnostic = std::move(decoded).error();
    metrics.bytes = input_bytes;
  }
  void adopt(const std::shared_ptr<const void>& previous) noexcept override {
    if (!previous)
      return;
    auto target = std::static_pointer_cast<const action_map>(previous);
    target->content_ = std::static_pointer_cast<const action_map>(value)->content_;
    value = std::move(target);
  }
};
std::shared_ptr<asset_loader> make_action_map_loader(asset_identity source,
                                                     std::shared_ptr<const asset_decoder> decoder) {
  return std::make_shared<action_map_loader>(std::move(source), std::move(decoder));
}
}
