// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstddef>
#include <gneiss/engine/resource/asset.hpp>
#include <memory>
#include <vector>
namespace gneiss {
namespace detail {
struct environment_loader;
}
/** 只读环境包资产；内容解码和 GPU 资源由 Render 管理，引用不表示 GPU 已就绪。 */
class environment final : public asset {
public:
  /** 所属线程获取不可变包；既有快照不随重载改变。 */
  [[nodiscard]] ref<const std::vector<std::byte>> snapshot() const {
    return ref<const std::vector<std::byte>>{content_};
  }

private:
  friend struct detail::environment_loader;
  environment(asset_identity source, std::shared_ptr<const std::vector<std::byte>> content)
      : asset(runtime_type_of<environment>()), content_(std::move(content)) {
    identity_ = std::move(source);
  }
  mutable std::shared_ptr<const std::vector<std::byte>> content_;
};
}
