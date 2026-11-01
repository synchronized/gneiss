// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_EDITOR_DOCUMENTS_SCENE_DOCUMENT_HPP_
#define GNEISS_EDITOR_DOCUMENTS_SCENE_DOCUMENT_HPP_

#include "engine/core/entity_id.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_subtree_snapshot.hpp"
#include "engine/function/world/world.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include "engine/resource/scene/scene_description.hpp"
namespace gneiss::runtime {
class application;
}

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gneiss::editor {

/** 将资产根内的本地路径转换为规范 asset URI；路径越界时拒绝。 */
[[nodiscard]] gneiss::engine::error make_asset_uri(const std::filesystem::path& asset_root,
                                                   const std::filesystem::path& path,
                                                   std::string& output) noexcept;

/** 生成符合场景 Schema 的随机 UUID。 */
[[nodiscard]] gneiss::engine::error make_editor_uuid(engine::guid& output) noexcept;

struct scene_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  gneiss::engine::guid uuid;
  std::string display_name;
  gneiss::engine::math::transform local_transform = {};
  engine::component_descriptions components{};
};

struct prefab_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  gneiss::engine::guid instance_uuid;
  gneiss::engine::guid source_node_uuid;
  std::string display_name;
  std::string prefab_uri;
  gneiss::engine::math::transform local_transform = {};
  gneiss::engine::math::transform source_local_transform = {};
  /** 当前源节点覆盖键的 UI 快照；以 Scene 覆盖列表为准。 */
  std::vector<engine::prefab_property_override_key> overrides{};
  [[nodiscard]] bool has_override(engine::guid type,
                                  engine::meta::property_id property) const noexcept {
    for (const auto& key : overrides)
      if (key.type_guid == type && key.property_id == property)
        return true;
    return false;
  }
  [[nodiscard]] bool has_overrides(engine::guid type) const noexcept {
    for (const auto& key : overrides)
      if (key.type_guid == type)
        return true;
    return false;
  }
  bool is_instance_root = false;
};

/** 由查询调用方持有的短期场景快照，不能用于写回场景。 */
struct scene_query_snapshot final {
  std::vector<scene_node_record> nodes;
  std::vector<prefab_node_record> prefab_nodes;
};

/** 创建网格节点的输入，不是当前场景的持久化快照。 */
struct mesh_node_creation final {
  gneiss::engine::guid uuid;
  gneiss::engine::guid parent_uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

using engine::scene_subtree_snapshot;

/** 单个场景的编辑文档：保存 URI、选择、脏状态及保存操作状态。
 * 借用 Application，绑定一个运行时场景实例；close 负责卸载该实例。
 * 节点查询返回独立快照，不在文档内缓存；保存和撤销从 World/Scene 捕获。
 * 不拥有 World 或第二棵运行时场景树；须先于 Application 关闭。
 * 撤销命令由宿主持有的 editor_command_history 管理，并在文档切换时清理。
 */
class scene_document final {
public:
  scene_document() noexcept = default;
  ~scene_document() noexcept { close(); }
  scene_document(const scene_document&) = delete;
  scene_document& operator=(const scene_document&) = delete;
  scene_document(scene_document&& other) noexcept;
  scene_document& operator=(scene_document&& other) noexcept;
  /** 接收已经激活的场景；成功后拥有句柄，所属线程调用。 */
  [[nodiscard]] gneiss::engine::error adopt(runtime::application& application,
                                            gneiss::engine::world* world,
                                            gneiss::scene_instance_id scene,
                                            std::string_view uri) noexcept;
  [[nodiscard]] gneiss::engine::error create_empty(runtime::application& application,
                                                   gneiss::engine::world* world,
                                                   gneiss::engine::guid scene_uuid) noexcept;
  [[nodiscard]] gneiss::engine::error create_empty(runtime::application& application,
                                                   gneiss::engine::world* world) noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  /** 使用已准备资源创建节点；成功后更新选择和脏状态，不加载资产。 */
  [[nodiscard]] engine::result<scene_node_id>
  create_mesh_node(const mesh_node_creation& description,
                   const engine::scene_mesh_binding& resources) noexcept;
  /** 捕获当前文档节点的资源强引用；文档关闭或应用停止后拒绝调用。 */
  [[nodiscard]] engine::result<engine::scene_mesh_binding>
  capture_mesh_binding(scene_node_id node) const noexcept;
  [[nodiscard]] gneiss::scene_instance_id scene_handle() const noexcept { return scene_; }
  /** 校验目标后调用提交；成功后标脏，不执行资产加载。 */
  [[nodiscard]] gneiss::engine::error
  commit_mesh_renderer(scene_node_id node,
                       const std::function<gneiss::engine::error()>& commit) noexcept;
  [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] engine::result<scene_node_record> query_node(scene_node_id node) const noexcept;
  [[nodiscard]] engine::result<prefab_node_record>
  query_prefab_node(scene_node_id node) const noexcept;
  /** 从 Scene 捕获本次查询的独立值，不在文档内缓存。 */
  [[nodiscard]] engine::result<scene_query_snapshot> capture_nodes() const noexcept;
  [[nodiscard]] scene_node_id selection() const noexcept { return selection_; }
  [[nodiscard]] engine::result<scene_node_record> selected_node() const noexcept;
  [[nodiscard]] engine::result<prefab_node_record> selected_prefab_node() const noexcept;
  [[nodiscard]] engine::result<prefab_node_record>
  find_prefab_root(gneiss::engine::guid instance_uuid) const noexcept;
  [[nodiscard]] engine::result<prefab_node_record>
  find_prefab_source(gneiss::engine::guid instance_uuid,
                     gneiss::engine::guid source_uuid) const noexcept;
  [[nodiscard]] engine::result<scene_node_record>
  find_node(gneiss::engine::guid uuid) const noexcept;

  [[nodiscard]] gneiss::engine::error select(scene_node_id node) noexcept;
  /** 清除失效选择并返回成功；文档或应用已失效时返回错误。 */
  [[nodiscard]] gneiss::engine::error validate_selection() noexcept;
  [[nodiscard]] gneiss::engine::error create_node(std::string_view name, scene_node_id parent,
                                                  scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::error rename_node(scene_node_id node,
                                                  std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::error reparent_node(scene_node_id node,
                                                    scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::error
  get_world_transform(scene_node_id node, gneiss::engine::math::transform& output) const noexcept;
  /** 设置节点局部 TRS；成功后标记场景已修改。 */
  [[nodiscard]] gneiss::engine::error
  set_local_transform(scene_node_id node, const gneiss::engine::math::transform& value) noexcept;
  /** 恢复 Prefab 来源节点的单个 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::error
  restore_prefab_transform_field(scene_node_id node, std::uint32_t field_id,
                                 gneiss::engine::math::transform& out_previous) noexcept;
  /** 恢复 Prefab 来源节点的全部 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::error
  restore_prefab_transform(scene_node_id node,
                           gneiss::engine::math::transform& out_previous) noexcept;
  [[nodiscard]] gneiss::engine::error
  destroy_subtree(scene_node_id node, scene_subtree_snapshot& out_snapshot) noexcept;
  [[nodiscard]] gneiss::engine::error restore_subtree(const scene_subtree_snapshot& snapshot,
                                                      scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::error duplicate_subtree(scene_node_id node, scene_node_id parent,
                                                        scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::error
  set_camera(scene_node_id node, const gneiss::engine::camera_description& desc) noexcept;
  [[nodiscard]] gneiss::engine::error remove_camera(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::error remove_mesh_renderer(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::error set_mesh_renderer(scene_node_id node,
                                                        std::string_view mesh_uri,
                                                        std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss::engine::error destroy_node(scene_node_id node) noexcept;
  /** 保存回执独立于文档生命周期；完成只写回执，宿主推进后显式接收。 */
  struct save_receipt {
    gneiss::scene_instance_id scene;
    std::string uri, content;
    engine::error status{engine::error::not_ready};
    bool complete{};
  };
  /** 提交当前内容的单文件安全保存；不阻塞、不修改 URI 或脏状态。 */
  [[nodiscard]] engine::error begin_save(gneiss::engine::files::service& files,
                                         std::string_view uri,
                                         gneiss::engine::files::write_mode mode,
                                         std::shared_ptr<save_receipt>& output) noexcept;
  /** 接收成功保存；场景实例不匹配时拒绝，保存期间的新修改保持 dirty。 */
  [[nodiscard]] engine::error accept_save(const save_receipt& receipt) noexcept;
  [[nodiscard]] engine::result<engine::component_descriptions>
  components(scene_node_id node) const noexcept;
  [[nodiscard]] engine::error
  set_component_description(scene_node_id node, const engine::component_description&) noexcept;
  [[nodiscard]] engine::error
  add_component_description(scene_node_id, const engine::component_description&) noexcept;
  [[nodiscard]] engine::error remove_component_description(scene_node_id, engine::guid) noexcept;
  [[nodiscard]] engine::result<engine::prefab_component_state>
      capture_component_override(scene_node_id, engine::guid) noexcept;
  [[nodiscard]] engine::error
  set_component_override(scene_node_id, engine::guid,
                         const engine::prefab_component_state&) noexcept;
  void mark_dirty() noexcept { is_dirty_ = true; }
  void clear_dirty() noexcept { is_dirty_ = false; }

private:
  engine::error encode_scene(std::string& output) const noexcept;
  template <class F>
  std::invoke_result_t<F, engine::scene_instances&> with_scene(F&& action) const noexcept;

  gneiss::engine::world* world_ = nullptr;
  // 借用宿主；宿主必须在关闭/销毁 Application 前关闭编辑文档。
  template <class F>
  engine::result<engine::scene_node_info> query_info(F&& predicate) const noexcept;
  runtime::application* application_{};
  gneiss::scene_instance_id scene_{};
  scene_node_id selection_;
  std::string uri_;
  bool is_dirty_ = false;
};

} // namespace gneiss::editor

#endif
