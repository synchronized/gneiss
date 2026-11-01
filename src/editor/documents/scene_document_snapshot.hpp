// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_document_change.hpp"
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
namespace gneiss::editor {
/** 作者历史持有的内存描述和资源引用；不保存节点地址，不读取文件。 */
struct scene_document_snapshot {
  std::string uri;
  engine::scene_instantiation_data description;
  engine::scene_instances::resource_bindings assets;
};
[[nodiscard]] engine::result<scene_document_snapshot>
capture_scene_document(runtime::application&, const scene_document&) noexcept;
struct prepared_scene_document {
  scene_document_snapshot snapshot;
  std::vector<engine::render_resource_source> missing;
};
/** 只计算内存变更及缺失依赖；missing 非空时必须补齐资源后才能采用快照。 */
[[nodiscard]] engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot&,
                       std::span<const author_document_change>) noexcept;
[[nodiscard]] engine::error restore_scene_document(runtime::application&, scene_document&,
                                                   const scene_document_snapshot&) noexcept;
/** 一次文档内存变更；所属线程 begin/advance，不驱动 I/O，不写来源文件。
 * application、document、history 须覆盖操作及历史命令寿命；取消或析构释放加载观察权。
 */
class scene_document_edit final {
public:
  scene_document_edit() = default;
  ~scene_document_edit() noexcept { cancel(); }
  scene_document_edit(const scene_document_edit&) = delete;
  scene_document_edit& operator=(const scene_document_edit&) = delete;
  engine::error begin(runtime::application&, scene_document&,
                      std::span<const author_document_change>, std::string label,
                      std::function<void(bool undo)> selection = {}) noexcept;
  /** not_ready 表示等待；终态重复调用不重复提交。 */
  engine::error advance(runtime::application&, scene_document&, editor_command_history&) noexcept;
  void cancel() noexcept;

private:
  engine::error finish(engine::error) noexcept;
  std::shared_ptr<scene_document_snapshot> before_, after_;
  std::uint64_t request_{};
  scene_instance_id scene_{};
  runtime::application* application_{};
  scene_document* document_{};
  std::string baseline_, label_;
  std::function<void(bool)> selection_;
  engine::error status_{engine::error::invalid_state};
};
}
