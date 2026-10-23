// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/resource/types/material_document.hpp"
#include <memory>

namespace gneiss::engine::resource {
class cpu_document_batch;
/** 所属线程使用的 CPU 文档共享入口，不拥有执行器、VFS 或渲染资源。
 * 批次共享内容索引，可晚于缓存外壳释放；索引访问仍须串行。 */
class cpu_document_cache final {
public:
  cpu_document_cache();
  ~cpu_document_cache();
  cpu_document_cache(const cpu_document_cache&) = delete;
  cpu_document_cache& operator=(const cpu_document_cache&) = delete;
  [[nodiscard]] std::shared_ptr<cpu_document_batch> begin();
  void collect();

private:
  friend class cpu_document_batch;
  struct state;
  std::shared_ptr<state> versions_;
};
/** 一批独占的文档候选；decode 在准备线程执行，retain_verified 在完成后由所属线程调用。
 * 两阶段不可并发；未发布候选随批次释放，不能复制批次所有者。 */
class cpu_document_batch final {
public:
  ~cpu_document_batch();
  cpu_document_batch(const cpu_document_batch&) = delete;
  cpu_document_batch& operator=(const cpu_document_batch&) = delete;
  gneiss_result decode(const std::vector<std::byte>& bytes, material_parameters& parameters,
                       std::array<std::string, 5>& uris, asset_diagnostic& diagnostic);
  /** 只接管各自来源校验成功的输出；失败项不妨碍其他项共享，不改变场景。 */
  gneiss_result retain_verified(prepared_batch& batch);

private:
  friend class cpu_document_cache;
  explicit cpu_document_batch(std::shared_ptr<cpu_document_cache::state> versions);
  std::shared_ptr<cpu_document_cache::state> versions_;
  std::vector<std::shared_ptr<const material_document>> candidates_;
};
}
