// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document.hpp"
#include "engine/function/scene/scene_instances.hpp"
namespace gneiss::editor {
/** 投影回执只保留轻量版本标识，不保活旧文档；失败后不能冒充已同步。 */
class scene_projection final {
public:
  using version = scene_document::version;
  bool is_synchronized(const version& current) const noexcept {
    return current && applied_ == current.stamp() && status_.ok();
  }
  engine::error status(const version& current) const noexcept {
    if (!current)
      return engine::error::invalid_state;
    return attempted_ == current.stamp() ? status_ : engine::error{engine::error::not_ready};
  }
  engine::error report(const version& current, const version& projected,
                       engine::error status) noexcept {
    if (!current || projected != current)
      return engine::error::invalid_state;
    attempted_ = current.stamp();
    status_ = status;
    applied_ = status.ok() ? current.stamp() : scene_document::revision{};
    return engine::error::success;
  }

private:
  scene_document::revision attempted_, applied_;
  engine::error status_{engine::error::not_ready};
};
/** 将目标文档投影到场景；baseline 仅可传入已成功同步的版本。
 * 资源身份不变时，普通对象增删、变换、组件及显式层级差异增量更新。
 * 失败不表示目标已同步。
 */
[[nodiscard]] engine::error project_scene_document(engine::world&, engine::scene_instances&,
                                                   scene_instance_id,
                                                   const scene_document_snapshot* baseline,
                                                   const scene_document_snapshot& target,
                                                   bool prepare_editor = true) noexcept;
}
