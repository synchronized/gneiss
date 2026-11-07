// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/resource/asset_uri.hpp"
#include <algorithm>
namespace gneiss::engine {
/** 引用需求不拥有资源；路径是出现位置，同一 URI 的不同位置分别保留。 */
struct asset_reference_requirement {
  type_id type;
  render_resource_source source;
  std::string path;
};
/** 先收集，再由 load 提交请求，最后绑定。收集不依赖服务、不读取文件或创建身份。
 * 通用解码收集时丢弃临时对象，避免保存随后可能移动的属性/容器地址。 */
class asset_reference_context final : public serialization::reference_resolver {
  std::vector<asset_reference_requirement> requirements_;
  std::vector<asset_request<render_resource_ref>> requests_;
  bool collecting_{};

public:
  const auto& dependencies() const noexcept { return requirements_; }
  const auto& requests() const noexcept { return requests_; }
  bool collecting() const noexcept override { return collecting_; }
  template <class Decode> serialization::diagnostic collect(Decode decode) {
    if (collecting_)
      return {.error = error::invalid_state};
    collecting_ = true;
    struct guard {
      asset_reference_context& owner;
      std::size_t checkpoint;
      bool committed{};
      ~guard() {
        owner.collecting_ = false;
        if (!committed)
          owner.requirements_.resize(checkpoint);
      }
    } reset{*this, requirements_.size()};
    serialization::context context{*this};
    auto temporary = decode(context);
    reset.committed = bool(temporary);
    return temporary ? serialization::diagnostic{} : std::move(temporary).error();
  }
  serialization::diagnostic record(type_id type, std::string_view uri, std::string_view path) {
    render_resource_kind kind;
    if (type == runtime_type_of<engine::texture>())
      kind = render_resource_kind::texture;
    else if (type == runtime_type_of<engine::mesh>())
      kind = render_resource_kind::mesh;
    else if (type == runtime_type_of<engine::material>())
      kind = render_resource_kind::material;
    else
      return {.error = error::unsupported};
    if (validate_uri(uri).failed())
      return {.error = error::invalid_argument};
    if (std::ranges::none_of(requirements_, [&](const auto& entry) {
          return entry.type == type && entry.source.uri == uri && entry.path == path;
        }))
      requirements_.push_back({type, {std::string(uri), kind}, std::string(path)});
    return {};
  }
  /** 仅提交需求，不等待内容；多个属性引用同一资产只保留一个请求。 */
  serialization::diagnostic load(asset_manager& assets) {
    for (const auto& entry : requirements_) {
      if (std::ranges::any_of(requests_, [&](const auto& request) {
            return resource_kind(request.resource()) == entry.source.type &&
                   resource_uri(request.resource()) == entry.source.uri;
          }))
        continue;
      auto request = assets.load(entry.source);
      if (!request)
        return {.path = entry.path, .message = entry.source.uri, .error = request.error()};
      requests_.push_back(std::move(*request));
    }
    return {};
  }
  result<ref<void>, serialization::diagnostic> resolve(type_id type, std::string_view uri,
                                                       std::string_view path = {}) override {
    if (collecting_) {
      auto status = record(type, uri, path);
      if (status.error.failed())
        return status;
      return ref<void>{};
    }
    for (const auto& request : requests_) {
      if (resource_uri(request.resource()) != uri)
        continue;
      auto cast = [&]<class T>() -> ref<void> {
        auto value = request.get<T>();
        return value.alias(static_cast<void*>(const_cast<T*>(value.get())));
      };
      if (type == runtime_type_of<engine::texture>() && request.get<engine::texture>())
        return cast.template operator()<engine::texture>();
      if (type == runtime_type_of<engine::mesh>() && request.get<engine::mesh>())
        return cast.template operator()<engine::mesh>();
      if (type == runtime_type_of<engine::material>() && request.get<engine::material>())
        return cast.template operator()<engine::material>();
    }
    return serialization::diagnostic{.message = "引用尚未由加载流程提交",
                                     .error = error::not_ready};
  }
};
}
