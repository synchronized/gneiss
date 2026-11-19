// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_EDITOR_DOCUMENTS_SCENE_EDIT_SESSION_HPP_
#define GNEISS_EDITOR_DOCUMENTS_SCENE_EDIT_SESSION_HPP_

#include "documents/document_patch.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_projection.hpp"
#include <functional>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/math/transform.hpp>
#include <gneiss/engine/function/files/service.hpp>
#include <gneiss/engine/function/scene/entity_id.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/scene/scene_description.hpp>
namespace gneiss::runtime {
class application;
}

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gneiss::editor {
class editor_command_history;
/** 单个 Prefab 组件的编辑快照；只属于文档与历史，不进入运行场景。 */
struct prefab_component_state {
  std::optional<gneiss::component_description> added{};
  bool removed = false;
  std::vector<gneiss::prefab_property_override> properties{};
  scene_mesh_binding resources{};
  gneiss::guid component_uuid{};
};

/** 将资产根内的本地路径转换为规范 asset URI；路径越界时拒绝。 */
[[nodiscard]] gneiss::error make_asset_uri(const std::filesystem::path& asset_root,
                                           const std::filesystem::path& path, std::string& output);

/** 生成符合场景 Schema 的随机 UUID。 */
[[nodiscard]] gneiss::error make_editor_uuid(gneiss::guid& output);

struct scene_node_record final {
  entity_id node;
  entity_id parent;
  entity_id entity;
  gneiss::guid uuid;
  std::string display_name;
  gneiss::math::transform local_transform = {};
  gneiss::component_descriptions components{};
};

struct prefab_node_record final {
  entity_id node;
  entity_id parent;
  entity_id entity;
  gneiss::guid instance_uuid;
  gneiss::guid source_node_uuid;
  std::string display_name;
  std::string prefab_uri;
  gneiss::math::transform local_transform = {};
  gneiss::math::transform source_local_transform = {};
  /** 当前源节点覆盖键的 UI 快照；以 Scene 覆盖列表为准。 */
  std::vector<gneiss::prefab_property_override_key> overrides{};
  [[nodiscard]] bool has_override(gneiss::guid type, gneiss::property_id property) const noexcept {
    for (const auto& key : overrides)
      if (key.target.target_type_guid == type && key.first_property() == property)
        return true;
    return false;
  }
  [[nodiscard]] bool has_overrides(gneiss::guid type) const noexcept {
    for (const auto& key : overrides)
      if (key.target.target_type_guid == type)
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
  gneiss::guid uuid;
  gneiss::object_address parent;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

/** 场景编辑会话：装配纯文档、运行投影、选择及保存协调。
 * 借用 Application，绑定一个运行时场景实例；close 负责卸载该实例。
 * 文档版本持有编辑内容；World 是投影，保存不读取运行时属性。
 * 不拥有 World 或第二棵运行时场景树；须先于 Application 和所属 World 关闭。
 * 撤销命令由宿主持有的 editor_command_history 管理，并在文档切换时清理。
 */
class scene_edit_session final {
public:
  scene_edit_session() noexcept = default;
  ~scene_edit_session() noexcept { close(); }
  scene_edit_session(const scene_edit_session&) = delete;
  scene_edit_session& operator=(const scene_edit_session&) = delete;
  scene_edit_session(scene_edit_session&& other) noexcept;
  scene_edit_session& operator=(scene_edit_session&& other) noexcept;
  /** 接收场景与本次加载的固定文档快照；成功后拥有句柄，不从 World 反推描述。所属线程调用。 */
  [[nodiscard]] gneiss::error adopt(runtime::application& application, gneiss::scene_instance scene,
                                    const scene_document_snapshot& initial,
                                    std::span<const gneiss::scene_object_binding> objects);
  [[nodiscard]] gneiss::error create_empty(runtime::application& application, gneiss::world* world,
                                           gneiss::guid scene_uuid);
  [[nodiscard]] gneiss::error create_empty(runtime::application& application, gneiss::world* world);
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  /** 使用已准备资源创建节点；成功后更新选择和脏状态，不加载资产。 */
  [[nodiscard]] gneiss::result<entity_id> create_mesh_node(const mesh_node_creation& description,
                                                           const scene_mesh_binding& resources);
  /** 捕获当前文档节点的资源强引用；文档关闭或应用停止后拒绝调用。 */
  [[nodiscard]] gneiss::result<scene_mesh_binding> capture_mesh_binding(entity_id node) const;
  [[nodiscard]] gneiss::scene_instance scene_handle() const noexcept { return scene_; }
  /** 替换已准备资源；当前绑定不匹配 expected 时拒绝，成功后标脏，不加载资产。 */
  [[nodiscard]] gneiss::error replace_mesh_binding(entity_id node,
                                                   const scene_mesh_binding& expected,
                                                   const scene_mesh_binding& replacement);
  [[nodiscard]] bool is_dirty() const noexcept { return document_ && document_->is_dirty(); }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] gneiss::result<scene_node_record> query_node(entity_id node) const;
  [[nodiscard]] gneiss::result<prefab_node_record> query_prefab_node(entity_id node) const;
  /** 从 Scene 捕获本次查询的独立值，不在文档内缓存。 */
  [[nodiscard]] gneiss::result<scene_query_snapshot> capture_nodes() const;
  [[nodiscard]] entity_id selection() const noexcept { return selection_; }
  [[nodiscard]] gneiss::result<scene_node_record> selected_node() const;
  [[nodiscard]] gneiss::result<prefab_node_record> selected_prefab_node() const;
  [[nodiscard]] gneiss::result<prefab_node_record>
  find_prefab_root(gneiss::guid instance_uuid) const;
  [[nodiscard]] gneiss::result<prefab_node_record>
  find_prefab_source(gneiss::guid instance_uuid, gneiss::guid source_uuid) const;
  [[nodiscard]] gneiss::result<scene_node_record> find_node(gneiss::guid uuid) const;

  [[nodiscard]] gneiss::error select(entity_id node);
  /** 清除失效选择并返回成功；文档或应用已失效时返回错误。 */
  [[nodiscard]] gneiss::error validate_selection();
  [[nodiscard]] gneiss::error create_node(std::string_view name, entity_id parent,
                                          entity_id& out_node);
  [[nodiscard]] gneiss::error rename_node(entity_id node, std::string_view name);
  [[nodiscard]] gneiss::error reparent_node(entity_id node, entity_id parent);
  [[nodiscard]] gneiss::error get_world_transform(entity_id node,
                                                  gneiss::math::transform& output) const;
  /** 设置节点局部 TRS；成功后标记场景已修改。 */
  [[nodiscard]] gneiss::error set_local_transform(entity_id node,
                                                  const gneiss::math::transform& value);
  /** 恢复 Prefab 来源节点的单个 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::error restore_prefab_transform_field(entity_id node, std::uint32_t field_id,
                                                             gneiss::math::transform& out_previous);
  /** 恢复 Prefab 来源节点的全部 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::error restore_prefab_transform(entity_id node,
                                                       gneiss::math::transform& out_previous);
  [[nodiscard]] gneiss::error destroy_subtree(entity_id node, scene_subtree_snapshot& out_snapshot);
  [[nodiscard]] gneiss::error restore_subtree(const scene_subtree_snapshot& snapshot,
                                              entity_id& out_node);
  [[nodiscard]] gneiss::error duplicate_subtree(entity_id node, entity_id parent,
                                                entity_id& out_node);
  [[nodiscard]] gneiss::error set_camera(entity_id node, const gneiss::camera_description& desc);
  [[nodiscard]] gneiss::error remove_camera(entity_id node);
  [[nodiscard]] gneiss::error remove_mesh_component(entity_id node);
  [[nodiscard]] gneiss::error set_mesh_component(entity_id node, std::string_view mesh_uri,
                                                 std::string_view material_uri);
  [[nodiscard]] gneiss::error destroy_node(entity_id node);
  /** 保存回执独立于文档生命周期；完成只写回执，宿主推进后显式接收。 */
  struct save_receipt {
    gneiss::scene_instance scene;
    scene_document::version version;
    std::string uri, content;
    gneiss::error status{gneiss::error::not_ready};
    bool complete{};
  };
  /** 提交当前内容的单文件安全保存；不阻塞、不修改 URI 或脏状态。 */
  [[nodiscard]] gneiss::error begin_save(gneiss::files::service& files, std::string_view uri,
                                         gneiss::files::write_mode mode,
                                         std::shared_ptr<save_receipt>& output);
  /** 接收成功保存；场景实例不匹配时拒绝，保存期间的新修改保持 dirty。 */
  [[nodiscard]] gneiss::error accept_save(const save_receipt& receipt);
  [[nodiscard]] gneiss::result<gneiss::component_descriptions> components(entity_id node) const;
  [[nodiscard]] gneiss::error set_component_description(entity_id node,
                                                        const gneiss::component_description&);
  [[nodiscard]] gneiss::error add_component_description(entity_id,
                                                        const gneiss::component_description&);
  [[nodiscard]] gneiss::error remove_component_description(entity_id, gneiss::guid);
  [[nodiscard]] gneiss::result<prefab_component_state> capture_component_override(entity_id,
                                                                                  gneiss::guid);
  [[nodiscard]] gneiss::error set_component_override(entity_id, gneiss::guid,
                                                     const prefab_component_state&);
  [[nodiscard]] gneiss::result<std::string> serialize() const;
  /** 当前编辑内容；不从 World 反向捕获。 */
  [[nodiscard]] gneiss::result<scene_document_snapshot> capture_content() const;
  /** 接受合法内容后尝试同步；返回值表示内容是否接受，同步错误单独查询。 */
  [[nodiscard]] gneiss::error accept_content(const scene_document_snapshot&);
  [[nodiscard]] gneiss::error synchronize();
  [[nodiscard]] gneiss::error projection_status() const noexcept;
  using version = scene_document::version;
  [[nodiscard]] version current_version() const noexcept {
    return edit_base_ ? edit_base_->current() : document_ ? document_->current() : version{};
  }
  /** 历史切换只接受当前基线；投影失败保留目标版本并由 projection_status 报告。 */
  [[nodiscard]] gneiss::error restore_changes(const scene_document::revision& expected,
                                              const scene_document::revision& target,
                                              std::span<const document_patch>, bool undo);
  [[nodiscard]] gneiss::error restore_version(const version& expected, const version& target);
  [[nodiscard]] gneiss::result<scene_subtree_snapshot> capture_subtree(entity_id) const;
  [[nodiscard]] gneiss::result<prefab_header_snapshot> capture_prefab_header(gneiss::guid) const;
  [[nodiscard]] gneiss::error apply_prefab(const scene_subtree_snapshot&, bool removing,
                                           bool replacing);
  void mark_dirty() {
    if (document_)
      document_->mark_dirty();
  }
  void clear_dirty() noexcept {
    if (document_)
      (void)document_->mark_saved(document_->current());
  }

private:
  friend gneiss::error execute_scene_edit(editor_command_history&, scene_edit_session&, std::string,
                                          const std::function<gneiss::error()>&, std::string);
  // 复合编辑期间借用权威文档，document_ 临时指向独立候选。
  const scene_document* edit_base_{};
  using content_edit =
      std::function<gneiss::error(gneiss::scene_description&, scene_document_snapshot&)>;
  gneiss::error edit_content(const content_edit&, const std::function<gneiss::error()>& = {});
  struct edit_target {
    gneiss::guid object, source;
    gneiss::game_object_description effective;
  };
  gneiss::result<edit_target> locate(entity_id) const;
  gneiss::error edit_component(entity_id, gneiss::guid, const gneiss::component_description&,
                               int mode);
  std::unique_ptr<scene_document> document_;
  scene_projection projection_;
  gneiss::error encode_scene(std::string& output) const;

  gneiss::world* world_ = nullptr;
  // 借用宿主；宿主必须在关闭/销毁 Application 前关闭编辑文档。
  gneiss::error report_projection(const version&, gneiss::error) noexcept;
  runtime::application* application_{};
  gneiss::scene_instance scene_{};
  entity_id selection_;
  std::string uri_;
};

} // namespace gneiss::editor

#endif
