// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_loader.hpp"
#include "engine/function/asset/asset_manager.hpp"
namespace gneiss::detail {
/** 只取得稳定身份，不把依赖的内容完成/GPU 完成作为父资产完成条件。
 * 身份强引用保活底层加载；非空但加载失败的引用仍保留原 URI。 */
template <class T> class reference_dependency final : public asset_dependency {
public:
  reference_dependency(std::string uri, asset_load_options options = {}, std::string path = {})
      : asset_dependency(runtime_type_of<T>(), std::move(uri), options, std::move(path)) {}
  error advance(asset_manager& assets) override {
    if (resource_)
      return error::success;
    auto submitted = assets.load<T>(uri, options);
    if (!submitted) {
      diagnostic = {submitted.error(), 0, path, uri};
      return diagnostic.error;
    }
    if constexpr (render_resource<T>)
      resource_ = submitted->template get<T>();
    else
      resource_ = submitted->resource();
    if (!resource_) {
      diagnostic = {error::unsupported, 0, path, "Loader 未提供稳定身份: " + uri};
      return diagnostic.error;
    }
    return error::success;
  }
  const ref<const T>& resource() const { return resource_; }

private:
  ref<const T> resource_;
};
}
