// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/resource/types/material_document.hpp"
#include <memory>

namespace gneiss::engine::resource {
/** 版本租约同时保活来源身份与只读 CPU 文档；不表示场景已采用该版本。 */
class material_document_lease final {
public:
  material_document_lease() = default;
  [[nodiscard]] const material_document* get() const noexcept { return document_.get(); }
  [[nodiscard]] const prepared_source& source() const noexcept { return source_; }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(document_); }

private:
  friend class material_document_versions;
  material_document_lease(prepared_source source, std::shared_ptr<const material_document> document)
      : source_(std::move(source)), document_(std::move(document)) {}
  prepared_source source_;
  std::shared_ptr<const material_document> document_;
};
}
