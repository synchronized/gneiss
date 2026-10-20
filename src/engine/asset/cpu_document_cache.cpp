// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/asset/cpu_document_cache.hpp"
#if defined(GNEISS_ENABLE_CAORS_MATERIAL) || defined(GNEISS_ENABLE_CAORS_MESH)
#include "engine/asset/caors_material_versions.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace gneiss::asset_internal {
namespace {
#ifdef GNEISS_ENABLE_CAORS_MESH
/** 数组只属于这个资源，别名 shared_ptr 通过 CAORS handle 保活。 */
struct mesh_document final : caors::resource {
  using caors::resource::resource;
  mesh_data data;
};
class mesh_documents final {
  using key = std::tuple<std::shared_ptr<const asset_source_domain>, std::string,
                         core::sha256_digest, std::uint64_t>;
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
    auto document = caors::resources::make_handle<mesh_document>(nullptr, caors::resource_guid{});
    if (!document)
      return GNEISS_ERROR_OUT_OF_MEMORY;
    document->data = std::move(asset.mesh);
    auto owner = std::make_shared<caors::resources::handle<mesh_document>>(std::move(document));
    std::shared_ptr<const mesh_data> data(owner, &(*owner)->data);
    versions_.insert_or_assign(std::move(identity), data);
    asset.shared_mesh = std::move(data);
    return GNEISS_SUCCESS;
  }
  void collect() {
    std::erase_if(versions_, [](const auto& item) { return item.second.expired(); });
  }
};
#endif
struct document_versions {
  material_document_versions materials;
#ifdef GNEISS_ENABLE_CAORS_MESH
  mesh_documents meshes;
#endif
};
class document_batch final : public cpu_document_batch {
public:
  explicit document_batch(std::shared_ptr<document_versions> versions)
      : versions_(std::move(versions)) {}
  gneiss_result decode(const std::vector<std::byte>& bytes, material_parameters& parameters,
                       std::array<std::string, 5>& uris, asset_diagnostic& diagnostic) override {
#ifdef GNEISS_ENABLE_CAORS_MATERIAL
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
#else
    asset_parsing::material_source source;
    const auto result = asset_parsing::parse_material(bytes, source, diagnostic);
    parameters = source.parameters;
    uris = std::move(source.texture_uris);
    return result;
#endif
  }
  gneiss_result retain_verified(prepared_batch& batch) override {
    std::size_t index = 0;
    for (auto& asset : batch.assets) {
#ifdef GNEISS_ENABLE_CAORS_MESH
      if (asset.source.type == asset_type::mesh) {
        const auto result = versions_->meshes.retain(asset);
        if (result != GNEISS_SUCCESS)
          return result;
        continue;
      }
#endif
#ifdef GNEISS_ENABLE_CAORS_MATERIAL
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
#else
      (void)asset;
#endif
    }
    return index == candidates_.size() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
  }

private:
  std::shared_ptr<document_versions> versions_;
  std::vector<caors::resources::handle<material_document>> candidates_;
};
class document_cache final : public cpu_document_cache {
public:
  std::shared_ptr<cpu_document_batch> begin() override {
    return std::make_shared<document_batch>(versions_);
  }
  void collect() override {
    versions_->materials.collect();
#ifdef GNEISS_ENABLE_CAORS_MESH
    versions_->meshes.collect();
#endif
  }

private:
  std::shared_ptr<document_versions> versions_ = std::make_shared<document_versions>();
};
}
std::shared_ptr<cpu_document_cache> make_cpu_document_cache() {
  return std::make_shared<document_cache>();
}
}
#else
namespace gneiss::asset_internal {
std::shared_ptr<cpu_document_cache> make_cpu_document_cache() { return {}; }
}
#endif
