// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/loaders/material_document_decoder.hpp"
#include "engine/resource/types/mesh_data.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace gneiss::engine::resource {
namespace {
class mesh_documents final {
  using key = std::tuple<std::shared_ptr<const asset_source_domain>, std::string,
                         engine::core::sha256_digest, std::uint64_t>;
  std::map<key, std::weak_ptr<const mesh_data>> contents_;

public:
  gneiss::engine::core::result retain(prepared_asset& asset) {
    if (asset.sources.size() != 1U || !asset.sources.front().identity.source ||
        asset.sources.front().identity.type != static_cast<std::uint32_t>(asset_type::mesh) ||
        asset.sources.front().identity.uri != asset.source.uri ||
        validate_uri(asset.source.uri) != gneiss::engine::core::result::success) {
      return gneiss::engine::core::result::invalid_state;
    }
    const auto& source = asset.sources.front();
    key identity{source.identity.source, source.identity.uri, source.digest, source.bytes};
    auto found = contents_.find(identity);
    if (found != contents_.end()) {
      if (auto existing = found->second.lock()) {
        asset.shared_mesh = std::move(existing);
        asset.mesh = {};
        return gneiss::engine::core::result::success;
      }
    }
    auto data = std::make_shared<const mesh_data>(std::move(asset.mesh));
    contents_.insert_or_assign(std::move(identity), data);
    asset.shared_mesh = std::move(data);
    return gneiss::engine::core::result::success;
  }
  void collect() {
    std::erase_if(contents_, [](const auto& item) { return item.second.expired(); });
  }
};
}
struct cpu_document_cache::state {
  using key = std::tuple<std::shared_ptr<const asset_source_domain>, std::string,
                         engine::core::sha256_digest, std::uint64_t>;
  std::map<key, std::weak_ptr<const material_document>> materials;
  mesh_documents meshes;
};
cpu_document_cache::cpu_document_cache() : contents_(std::make_shared<state>()) {}
cpu_document_cache::~cpu_document_cache() = default;
std::shared_ptr<cpu_document_batch> cpu_document_cache::begin() {
  return std::shared_ptr<cpu_document_batch>(new cpu_document_batch(contents_));
}
void cpu_document_cache::collect() {
  std::erase_if(contents_->materials, [](const auto& item) { return item.second.expired(); });
  contents_->meshes.collect();
}
cpu_document_batch::cpu_document_batch(std::shared_ptr<cpu_document_cache::state> contents)
    : contents_(std::move(contents)) {}
cpu_document_batch::~cpu_document_batch() = default;
gneiss::engine::core::result cpu_document_batch::decode(const std::vector<std::byte>& bytes,
                                                        material_parameters& parameters,
                                                        std::array<std::string, 5>& uris,
                                                        asset_diagnostic& diagnostic) {
  // 解码证据随加载任务保存，文档本身只包含作者数据。
  auto document = std::make_shared<material_document>();
  if (!document) {
    return gneiss::engine::core::result::out_of_memory;
  }
  const auto result = decode_material_document(bytes, *document, diagnostic);
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  parameters = document->source.parameters;
  uris = document->source.texture_uris;
  decoded_.push_back({.digest = engine::core::sha256(bytes),
                      .bytes = bytes.size(),
                      .document = std::move(document)});
  return gneiss::engine::core::result::success;
}
gneiss::engine::core::result cpu_document_batch::retain_prepared(prepared_batch& batch) {
  for (auto& asset : batch.assets) {
    if (asset.source.type == asset_type::mesh) {
      const auto result = contents_->meshes.retain(asset);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      continue;
    }
    if (asset.source.type != asset_type::material) {
      continue;
    }
    if (asset.sources.size() != 1U || !asset.sources.front().identity.source ||
        asset.sources.front().identity.type != static_cast<std::uint32_t>(asset_type::material) ||
        asset.sources.front().identity.uri != asset.source.uri) {
      return gneiss::engine::core::result::invalid_state;
    }
    const auto& source = asset.sources.front();
    const auto document = std::ranges::find_if(decoded_, [&](const auto& decoded) {
      return decoded.digest == source.digest && decoded.bytes == source.bytes;
    });
    if (document == decoded_.end()) {
      return gneiss::engine::core::result::invalid_state;
    }
    auto& weak =
        contents_
            ->materials[{source.identity.source, source.identity.uri, source.digest, source.bytes}];
    asset.material_document = weak.lock();
    if (!asset.material_document) {
      asset.material_document = document->document;
      weak = asset.material_document;
    }
  }
  // 失败资源留下的解析结果随批次释放，不能阻止其他已校验资源共享。
  return gneiss::engine::core::result::success;
}

}
