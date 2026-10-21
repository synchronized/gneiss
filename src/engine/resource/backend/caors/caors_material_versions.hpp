// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/backend/caors/caors_material_document.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <map>

namespace gneiss::engine::resource {
/** 版本租约同时保活来源身份与只读 CPU 文档；不表示场景已采用该版本。 */
class material_document_lease final : public material_document_reference {
public:
  material_document_lease() = default;
  [[nodiscard]] const material_document* get() const noexcept { return document_.get(); }
  [[nodiscard]] const prepared_source& source() const noexcept { return source_; }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(document_); }

private:
  friend class material_document_versions;
  material_document_lease(prepared_source source,
                          caors::resources::handle<material_document> document)
      : source_(std::move(source)), document_(std::move(document)) {}
  prepared_source source_;
  // CAORS 当前不支持 handle<const T>；只通过租约暴露 const 访问。
  caors::resources::handle<material_document> document_;
};
/** 所属线程串行使用的非拥有索引，不创建 manager、线程或第二份强引用缓存。
 * 调用方只可在来源复验成功后登记；登记校验回执与文档一致，不自行进行文件复读。
 * GUID 仅由 CAORS 管理，不能替代来源身份或内容摘要。 */
class material_document_versions final {
  struct key {
    asset_identity identity;
    core::sha256_digest digest;
    std::uint64_t bytes;
  };
  struct less {
    bool operator()(const key& a, const key& b) const noexcept {
      if (a.identity.source.get() != b.identity.source.get())
        return std::less<const asset_source_domain*>{}(a.identity.source.get(),
                                                       b.identity.source.get());
      if (a.identity.uri != b.identity.uri)
        return a.identity.uri < b.identity.uri;
      if (a.identity.type != b.identity.type)
        return a.identity.type < b.identity.type;
      if (a.digest != b.digest)
        return a.digest < b.digest;
      return a.bytes < b.bytes;
    }
  };

public:
  /** 同一来源和内容复用已有对象；来源不一致不得合并。失败清空输出，不改变索引。 */
  [[nodiscard]] result remember(const prepared_source& verified,
                                caors::resources::handle<material_document> document,
                                material_document_lease& output) noexcept {
    output = {};
    if (!verified.identity.source ||
        verified.identity.type != static_cast<std::uint32_t>(asset_type::material) ||
        validate_uri(verified.identity.uri) != GNEISS_SUCCESS || !document ||
        document->digest != verified.digest || document->source_bytes != verified.bytes)
      return result::invalid_argument;
    try {
      key version{verified.identity, verified.digest, verified.bytes};
      material_document_lease candidate{verified, std::move(document)};
      auto found = versions_.find(version);
      if (found != versions_.end()) {
        if (auto existing = found->second.lock())
          candidate.document_ = std::move(existing);
        else
          found->second = candidate.document_;
      } else
        versions_.emplace(std::move(version), candidate.document_);
      output = std::move(candidate);
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }
  /** 只查询确切已验证版本；不会按 URI 返回“最新”，也不代替本次来源复验。 */
  [[nodiscard]] result find(const prepared_source& version,
                            material_document_lease& output) const noexcept {
    output = {};
    try {
      const auto found = versions_.find({version.identity, version.digest, version.bytes});
      if (found == versions_.end())
        return result::not_ready;
      auto document = found->second.lock();
      if (!document)
        return result::not_ready;
      output = {version, std::move(document)};
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }
  /** 回收失效弱项及其来源标记；不会使仍有租约的文档失效。 */
  void collect() {
    std::erase_if(versions_, [](const auto& item) { return item.second.expired(); });
  }

private:
  std::map<key, caors::resources::weak_handle<material_document>, less> versions_;
};
} // namespace gneiss::engine::resource
