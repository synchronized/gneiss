// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include <algorithm>
namespace gneiss::engine {
/** 所属线程的资源引用解析；仅预留身份并记录依赖，不启动文件加载。
 * manager 必须覆盖上下文寿命，依赖引用由解码对象持有。 */
class asset_reference_context final : public serialization::reference_resolver {
  asset_manager& assets_;
  std::vector<render_resource_source> dependencies_;

public:
  explicit asset_reference_context(asset_manager& assets) noexcept : assets_(assets) {}
  const std::vector<render_resource_source>& dependencies() const noexcept { return dependencies_; }
  result<ref<void>, serialization::diagnostic> resolve(type_id type,
                                                       std::string_view uri) override {
    render_resource_kind kind;
    if (type == runtime_type_of<texture_resource>())
      kind = render_resource_kind::texture;
    else if (type == runtime_type_of<mesh_resource>())
      kind = render_resource_kind::mesh;
    else if (type == runtime_type_of<material_resource>())
      kind = render_resource_kind::material;
    else
      return serialization::diagnostic{.error = error::unsupported};
    render_resource_source source{std::string(uri), kind};
    auto reserved = assets_.reserve_resource(source);
    if (!reserved)
      return serialization::diagnostic{.message = std::string(uri), .error = reserved.error()};
    if (std::ranges::none_of(
            dependencies_, [&](const auto& item) { return item.uri == uri && item.type == kind; }))
      dependencies_.push_back(source);
    // 引用绑定只向 ref<const T> 交付所有权；类型擦除接口不修改资源内容。
    if (kind == render_resource_kind::texture) {
      auto value = reserved->as<texture_resource>();
      return value.alias(static_cast<void*>(const_cast<texture_resource*>(value.get())));
    }
    if (kind == render_resource_kind::mesh) {
      auto value = reserved->as<mesh_resource>();
      return value.alias(static_cast<void*>(const_cast<mesh_resource*>(value.get())));
    }
    auto value = reserved->as<material_resource>();
    return value.alias(static_cast<void*>(const_cast<material_resource*>(value.get())));
  }
};
}
