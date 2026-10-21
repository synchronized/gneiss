// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/loaders/asset_preparation.hpp"

namespace gneiss::asset_internal {
/** 一批独占的文档候选；decode 在准备线程执行，retain_verified 在完成后由所属线程调用。 */
class cpu_document_batch {
public:
  virtual ~cpu_document_batch() = default;
  virtual gneiss_result decode(const std::vector<std::byte>& bytes, material_parameters& parameters,
                               std::array<std::string, 5>& uris, asset_diagnostic& diagnostic) = 0;
  /** 仅接收整批来源复验成功的输出；不改变渲染版本选择或场景。 */
  virtual gneiss_result retain_verified(prepared_batch& batch) = 0;
};
/** 所属线程使用的 CPU 文档共享入口，不拥有执行器、VFS 或渲染资源。 */
class cpu_document_cache {
public:
  virtual ~cpu_document_cache() = default;
  virtual std::shared_ptr<cpu_document_batch> begin() = 0;
  virtual void collect() = 0;
};
/** 创建统一 CPU 材质与网格缓存，不要求线程池或 I/O 后端。 */
std::shared_ptr<cpu_document_cache> make_cpu_document_cache();
}
