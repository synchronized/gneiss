// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/platform/caors/document_loader.hpp"
#include "engine/resource/loaders/asset_parsing.hpp"

namespace gneiss::engine::resource {
/** CPU 文档，不持有纹理或 RID；发布后由宿主按不可变对象使用。 */
struct material_document final : caors::resource {
  using caors::resource::resource;
  asset_parsing::material_source source;
  core::sha256_digest digest{};
  std::uint64_t source_bytes{};
};
/** 复用批次已经读取的字节；不进行 I/O、来源复验或共享版本登记。 */
inline gneiss_result decode_material_document(const std::vector<std::byte>& bytes,
                                              material_document& value,
                                              asset_diagnostic& diagnostic) {
  const auto parsed = asset_parsing::parse_material(bytes, value.source, diagnostic);
  if (parsed == GNEISS_SUCCESS) {
    value.digest = core::sha256(bytes);
    value.source_bytes = bytes.size();
  }
  return parsed;
}
using material_document_loader = caors_internal::document_loader<material_document>;
/** 独立接入验证入口；默认 1 MiB，宿主可传入单文档预算，须与 VFS 桥及复验一致。 */
inline std::unique_ptr<material_document_loader> make_material_document_loader(
    caors_internal::file_system_adapter& files,
    std::function<result(std::string_view, std::span<const std::byte>)> verify = {},
    std::size_t maximum_document_bytes = 1024U * 1024U) {
  return std::make_unique<caors_internal::bounded_document_loader<material_document>>(
      files,
      [](auto bytes, material_document& value) {
        // 复用现有解析器；复制和摘要都发生在 worker。
        std::vector<std::byte> owned(bytes.begin(), bytes.end());
        asset_diagnostic diagnostic;
        const auto parsed = decode_material_document(owned, value, diagnostic);
        return result{parsed};
      },
      std::move(verify), maximum_document_bytes);
}
} // namespace gneiss::engine::resource
