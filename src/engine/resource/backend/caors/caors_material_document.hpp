// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/platform/caors/document_loader.hpp"
#include "engine/resource/loaders/material_document_decoder.hpp"

namespace gneiss::engine::resource {
using material_document_loader = engine::platform::caors::document_loader<material_document>;
/** 独立接入验证入口；默认 1 MiB，宿主可传入单文档预算，须与 VFS 桥及复验一致。 */
inline std::unique_ptr<material_document_loader> make_material_document_loader(
    engine::platform::caors::file_system_adapter& files,
    std::function<result(std::string_view, std::span<const std::byte>)> verify = {},
    std::size_t maximum_document_bytes = 1024U * 1024U) {
  return std::make_unique<engine::platform::caors::bounded_document_loader<material_document>>(
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
