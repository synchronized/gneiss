// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_loader.hpp"
#include "engine/function/asset/asset_manager.hpp"
namespace gneiss::engine::detail {
/** 类型化结果槽；声明阶段不读取文件，只有管理器推进时才提交请求。 */
template <class T, class Content> class content_dependency final : public asset_dependency {
public:
  explicit content_dependency(std::string uri, bool reload, Content (*capture)(const T&))
      : asset_dependency(runtime_type_of<T>(), std::move(uri), {.reload = reload}),
        capture_(capture) {}
  error advance(asset_manager& assets) override {
    if (settled_)
      return diagnostic.error;
    if (!request_) {
      auto submitted = assets.load<T>(uri, options);
      if (!submitted) {
        if (submitted.error() == error::not_ready)
          return error::not_ready;
        return fail({submitted.error(), 0, {}, "无法提交必要资产依赖"});
      }
      request_.emplace(std::move(*submitted));
    }
    if constexpr (requires { Content{request_->snapshot()}; })
      content_ = Content{request_->snapshot()};
    auto loaded = request_->take();
    if (!loaded) {
      if (loaded.error().error == error::not_ready)
        return error::not_ready;
      return fail(std::move(loaded).error());
    }
    bytes_ = request_->source_bytes();
    resource_ = std::move(*loaded);
    if constexpr (!requires { Content{request_->snapshot()}; })
      content_ = capture_(*resource_);
    request_.reset();
    settled_ = true;
    return error::success;
  }
  const ref<const T>& resource() const { return resource_; }
  std::size_t source_bytes() const noexcept { return bytes_; }
  const Content& content() const noexcept { return content_; }

private:
  error fail(asset_diagnostic failure) {
    failure.path = uri + (failure.path.empty() ? "" : ":" + failure.path);
    diagnostic = std::move(failure);
    request_.reset();
    settled_ = true;
    return diagnostic.error;
  }
  bool settled_{};
  std::optional<asset_request<T>> request_;
  ref<const T> resource_;
  std::size_t bytes_{};
  Content (*capture_)(const T&);
  Content content_;
};
}
