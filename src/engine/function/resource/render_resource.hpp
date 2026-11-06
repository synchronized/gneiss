// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <concepts>
namespace gneiss::engine {
enum class render_resource_kind : std::uint32_t {
  invalid = 0U,
  mesh = 1U,
  material = 2U,
  texture = 3U
};
struct render_resource_source final {
  std::string uri;
  render_resource_kind type = render_resource_kind::mesh;
};
/** 资源分类属于加载层，不让资源对象反向依赖 render_resource_kind。 */
template <typename T> struct resource_traits {};
template <> struct resource_traits<mesh_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::mesh;
};
template <> struct resource_traits<texture_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::texture;
};
template <> struct resource_traits<material_resource> {
  static constexpr render_resource_kind kind = render_resource_kind::material;
};
template <typename T>
concept render_resource = std::derived_from<T, asset> && requires {
  { resource_traits<T>::kind } -> std::convertible_to<render_resource_kind>;
};

/** 混合资源强引用；直接持有类型化资源，不暴露缓存条目或创建新拥有者。 */
class render_resource_ref final {
public:
  render_resource_ref() noexcept = default;
  template <render_resource T> render_resource_ref(asset_ref<const T> value) noexcept {
    value_ = std::move(value);
  }
  template <class T> [[nodiscard]] asset_ref<const T> as() const noexcept {
    if constexpr (render_resource<T>)
      return asset_cast<T>(value_);
    else
      return {};
  }
  [[nodiscard]] explicit operator bool() const noexcept { return bool(value_); }
  void reset() noexcept { value_.reset(); }
  friend bool operator==(const render_resource_ref&, const render_resource_ref&) = default;

private:
  ref<const asset> value_;
};
template <class T>
[[nodiscard]] asset_ref<const T> resource_cast(const render_resource_ref& value) noexcept {
  return value.as<T>();
}
[[nodiscard]] inline render_resource_kind resource_kind(const render_resource_ref& value) noexcept {
  if (value.as<mesh_resource>())
    return render_resource_kind::mesh;
  if (value.as<texture_resource>())
    return render_resource_kind::texture;
  if (value.as<material_resource>())
    return render_resource_kind::material;
  return render_resource_kind::invalid;
}
[[nodiscard]] inline rid resource_rid(const render_resource_ref& value) noexcept {
  if (auto asset = value.as<mesh_resource>())
    return resource_handle(asset);
  if (auto asset = value.as<texture_resource>())
    return resource_handle(asset);
  return resource_handle(value.as<material_resource>());
}
[[nodiscard]] inline std::string_view resource_uri(const render_resource_ref& value) noexcept {
  if (auto asset = value.as<mesh_resource>())
    return asset->source().uri;
  if (auto asset = value.as<texture_resource>())
    return asset->source().uri;
  if (auto asset = value.as<material_resource>())
    return asset->source().uri;
  return {};
}

template <render_resource T>
[[nodiscard]] inline std::string_view resource_uri(const asset_ref<const T>& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
template <render_resource T>
[[nodiscard]] inline rid resource_rid(const asset_ref<const T>& value) noexcept {
  return resource_handle(value);
}
template <render_resource T>
[[nodiscard]] inline render_resource_kind resource_kind(const asset_ref<const T>& value) noexcept {
  return value ? resource_traits<T>::kind : render_resource_kind::invalid;
}

} // namespace gneiss::engine
