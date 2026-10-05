// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "editor_command_history.hpp"
#include "editor_session.hpp"

namespace gneiss::editor {

/** 调用期间借用的 Mesh Renderer 资产配对，不持有资源或 URI。 */
struct mesh_asset_reference final {
  std::string_view mesh_uri;
  std::string_view material_uri;
};

/** 主线程作者编辑；命令拥有身份和 URI，session 必须活到历史清空。
 * 失败不记录命令；历史记录失败时回滚并恢复原脏状态。 */
[[nodiscard]] result add_mesh_asset(editor_session& session, editor_command_history& history,
                                    std::string_view name, mesh_asset_reference assets) noexcept;
[[nodiscard]] result add_prefab_asset(editor_session& session, editor_command_history& history,
                                      std::string_view name, std::string_view prefab_uri,
                                      scene_node_id parent) noexcept;
[[nodiscard]] result replace_mesh_assets(editor_session& session, editor_command_history& history,
                                         std::string_view node_uuid,
                                         mesh_asset_reference assets) noexcept;

} // namespace gneiss::editor
