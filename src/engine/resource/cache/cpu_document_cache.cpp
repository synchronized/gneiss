// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/resource/cache/material_document_versions.hpp"
#include "engine/resource/loaders/material_document_decoder.hpp"
#include "engine/resource/types/mesh_document.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace gneiss::engine::resource {
namespace {
class mesh_documents final {
  using key = std::tuple<std::shared_ptr<const asset_source_domain>, std::string,
                         engine::core::sha256_digest, std::uint64_t>;
  std::map<key, std::weak_ptr<const mesh_data>> versions_;

public:
  gneiss_result retain(prepared_asset& asset) {
    if (asset.sources.size() != 1U || !asset.sources.front().identity.source ||
        asset.sources.front().identity.type != static_cast<std::uint32_t>(asset_type::mesh) ||
        asset.sources.front().identity.uri != asset.source.uri ||
        validate_uri(asset.source.uri) != GNEISS_SUCCESS || asset.version.version.empty()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    const auto& source = asset.sources.front();
    key identity{source.identity.source, source.identity.uri, source.digest, source.bytes};
    auto found = versions_.find(identity);
    if (found != versions_.end()) {
      if (auto existing = found->second.lock()) {
        asset.shared_mesh = std::move(existing);
        asset.mesh = {};
        return GNEISS_SUCCESS;
      }
    }
    auto document =
        ::caors::resources::make_handle<mesh_document>(nullptr, ::caors::resource_guid{});
    if (!document)
      return GNEISS_ERROR_OUT_OF_MEMORY;
    document->data = std::move(asset.mesh);
    auto owner = std::make_shared<::caors::resources::handle<mesh_document>>(std::move(document));
    std::shared_ptr<const mesh_data> data(owner, &(*owner)->data);
    versions_.insert_or_assign(std::move(identity), data);
    asset.shared_mesh = std::move(data);
    return GNEISS_SUCCESS;
  }
  void collect() {
    std::erase_if(versions_, [](const auto& item) { return item.second.expired(); });
  }
};
struct document_versions {
  material_document_versions materials;
  mesh_documents meshes;
};
class document_batch final : public cpu_document_batch {
public:
  explicit document_batch(std::shared_ptr<document_versions> versions)
      : versions_(std::move(versions)) {}
  gneiss_result decode(const std::vector<std::byte>& bytes, material_parameters& parameters,
                       std::array<std::string, 5>& uris, asset_diagnostic& diagnostic) override {
    // GUID 不参与版本选择；候选不注册到按 URI reload 的可变槽位。
    auto document =
        ::caors::resources::make_handle<material_document>(nullptr, ::caors::resource_guid{});
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
      if (asset.source.type == asset_type::mesh) {
        const auto result = versions_->meshes.retain(asset);
        if (result != GNEISS_SUCCESS)
          return result;
        continue;
      }
      if (asset.source.type != asset_type::material) {
        continue;
      }
      if (index >= candidates_.size() || asset.sources.empty()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      auto lease = std::make_shared<material_document_lease>();
      const auto result =
          versions_->materials.remember(asset.sources.front(), candidates_[index++], *lease);
      if (result != result::success) {
        return static_cast<gneiss_result>(result);
      }
      asset.material_document = std::move(lease);
    }
    return index == candidates_.size() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
  }

private:
  std::shared_ptr<document_versions> versions_;
  std::vector<::caors::resources::handle<material_document>> candidates_;
};
class document_cache final : public cpu_document_cache {
public:
  std::shared_ptr<cpu_document_batch> begin() override {
    return std::make_shared<document_batch>(versions_);
  }
  void collect() override {
    versions_->materials.collect();
    versions_->meshes.collect();
  }

private:
  std::shared_ptr<document_versions> versions_ = std::make_shared<document_versions>();
};
}
std::shared_ptr<cpu_document_cache> make_cpu_document_cache() {
  return std::make_shared<document_cache>();
}
}
