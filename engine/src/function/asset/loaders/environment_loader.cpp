// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/asset/detail/loader_factory.hpp"
#include <gneiss/engine/resource/environment.hpp>
namespace gneiss::detail {
struct environment_loader final : asset_loader {
  asset_identity source;
  explicit environment_loader(asset_identity identity)
      : asset_loader(identity.uri), source(std::move(identity)) {}
  std::shared_ptr<const asset> reserve() override {
    return std::shared_ptr<const environment>(new environment(source, {}));
  }
  void decode(const std::vector<std::byte>& bytes) override {
    if (bytes.empty() || bytes.size() > 64U * 1024U * 1024U) {
      reject({error::invalid_argument, 0, source.uri, "环境包为空或超过 64 MiB"});
      return;
    }
    value = std::shared_ptr<const environment>(
        new environment(source, std::make_shared<const std::vector<std::byte>>(bytes)));
    metrics.bytes = input_bytes;
    status = error::success;
  }
  void adopt(const std::shared_ptr<const void>& previous) override {
    if (!previous)
      return;
    auto target = std::static_pointer_cast<const environment>(previous);
    target->content_ = std::static_pointer_cast<const environment>(value)->content_;
    value = std::move(target);
  }
};
std::shared_ptr<asset_loader> make_environment_loader(asset_identity source) {
  return std::make_shared<environment_loader>(std::move(source));
}
}
