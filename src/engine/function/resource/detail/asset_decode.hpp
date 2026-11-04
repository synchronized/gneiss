// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/resource/asset_loading.hpp"
#include "engine/function/resource/render_resource.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::engine {
/** 解码结果只有一个有效内容；材质共享已经解码的不可变描述。 */
using decoded_content =
    std::variant<std::monostate, mesh_data, texture_data, asset_ref<const material_description>>;
struct decoded_asset {
  std::string uri;
  std::vector<asset_read_record> sources;
  decoded_content content;
  std::size_t bytes{};
  [[nodiscard]] render_resource_kind kind() const noexcept {
    if (std::holds_alternative<mesh_data>(content))
      return render_resource_kind::mesh;
    if (std::holds_alternative<texture_data>(content))
      return render_resource_kind::texture;
    if (std::holds_alternative<asset_ref<const material_description>>(content))
      return render_resource_kind::material;
    return render_resource_kind::invalid;
  }
};
/** 纯内存网格解码；失败返回诊断，不读取文件或创建渲染资源。 */
[[nodiscard]] result<decoded_asset, asset_diagnostic>
decode_mesh_asset(std::string_view uri, const std::vector<std::byte>& bytes);
/** 描述已解析，负载已读取；按 profile 选择纹理内容，失败返回诊断。 */
[[nodiscard]] result<decoded_asset, asset_diagnostic>
decode_texture_asset(std::string_view uri, const texture_description& description,
                     const std::vector<std::byte>& payload, texture_prepare_profile profile);
/** 采用已解码材质，统计大小并共享原引用，不再次解析或复制参数和依赖。 */
[[nodiscard]] result<decoded_asset, asset_diagnostic>
adopt_material_asset(std::string_view uri, asset_ref<const material_description> material,
                     std::size_t material_bytes);

} // namespace gneiss::engine
