// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/asset/material_document_cache.hpp"
#ifdef GNEISS_ENABLE_CAORS_MATERIAL
#include "engine/asset/caors_material_versions.hpp"

namespace gneiss::asset_internal {
namespace {
class document_batch final : public material_document_batch {
public:
  explicit document_batch(std::shared_ptr<material_document_versions> versions)
      : versions_(std::move(versions)) {}
  gneiss_result decode(const std::vector<std::byte>& bytes, material_parameters& parameters,
                       std::array<std::string, 5>& uris, asset_diagnostic& diagnostic) override {
    // GUID 不参与版本选择；候选不注册到按 URI reload 的可变槽位。
    auto document =
        caors::resources::make_handle<material_document>(nullptr, caors::resource_guid{});
    if (!document) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    const auto result = decode_material_document(bytes, *document, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    parameters = document->source.parameters;
    uris = document->source.texture_uris;
    candidates_.push_back(std::move(document));
    return GNEISS_SUCCESS;
  }
  gneiss_result retain_verified(prepared_batch& batch) override {
    std::size_t index = 0;
    for (auto& asset : batch.assets) {
      if (asset.source.type != asset_type::material) {
        continue;
      }
      if (index >= candidates_.size() || asset.sources.empty()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      auto lease = std::make_shared<material_document_lease>();
      const auto result = versions_->remember(asset.sources.front(), candidates_[index++], *lease);
      if (result != result::success) {
        return static_cast<gneiss_result>(result);
      }
      asset.material_document = std::move(lease);
    }
    return index == candidates_.size() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
  }

private:
  std::shared_ptr<material_document_versions> versions_;
  std::vector<caors::resources::handle<material_document>> candidates_;
};
class document_cache final : public material_document_cache {
public:
  std::shared_ptr<material_document_batch> begin() override {
    return std::make_shared<document_batch>(versions_);
  }
  void collect() override { versions_->collect(); }

private:
  std::shared_ptr<material_document_versions> versions_ =
      std::make_shared<material_document_versions>();
};
}
std::shared_ptr<material_document_cache> make_material_document_cache() {
  return std::make_shared<document_cache>();
}
}
#else
namespace gneiss::asset_internal {
std::shared_ptr<material_document_cache> make_material_document_cache() { return {}; }
}
#endif
