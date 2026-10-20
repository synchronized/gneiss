// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "asset_parsing.hpp"
#include "engine/platform/caors/document_loader.hpp"

namespace gneiss::asset_internal {
/** CPU 文档，不持有纹理或 RID；发布后由宿主按不可变对象使用。 */
struct material_document final : caors::resource {
  using caors::resource::resource;
  asset_parsing::material_source source;
  core::sha256_digest digest{};
};
using material_document_loader = caors_internal::document_loader<material_document>;
/** 独立接入验证入口；最多 1 MiB，不替换生产材质接口的大小契约。 */
inline std::unique_ptr<material_document_loader> make_material_document_loader(
    caors_internal::file_system_adapter& files,
    std::function<result(std::string_view, std::span<const std::byte>)> verify = {}) {
  return std::make_unique<caors_internal::bounded_document_loader<material_document>>(
      files,
      [](auto bytes, material_document& value) {
        // 复用现有解析器；复制和摘要都发生在 worker。
        std::vector<std::byte> owned(bytes.begin(), bytes.end());
        asset_diagnostic diagnostic;
        const auto parsed = asset_parsing::parse_material(owned, value.source, diagnostic);
        if (parsed == GNEISS_SUCCESS)
          value.digest = core::sha256(owned);
        return result{parsed};
      },
      std::move(verify));
}
} // namespace gneiss::asset_internal
