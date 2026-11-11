// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_EDITOR_DOCUMENTS_SCENE_EDIT_SESSION_HPP_
#define GNEISS_EDITOR_DOCUMENTS_SCENE_EDIT_SESSION_HPP_

#include "documents/document_patch.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_projection.hpp"
#include "engine/core/entity_id.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <functional>
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
  std::optional<engine::component_description> added{};
  bool removed = false;
  std::vector<engine::prefab_property_override> properties{};
  scene_mesh_binding resources{};
  engine::guid component_uuid{};
};

/** 将资产根内的本地路径转换为规范 asset URI；路径越界时拒绝。 */
[[nodiscard]] gneiss::engine::error make_asset_uri(const std::filesystem::path& asset_root,
                                                   const std::filesystem::path& path,
                                                   std::string& output);

/** 生成符合场景 Schema 的随机 UUID。 */
[[nodiscard]] gneiss::engine::error make_editor_uuid(engine::guid& output);

struct scene_node_record final {
  entity_id node;
  entity_id parent;
  entity_id entity;
  gneiss::engine::guid uuid;
  std::string display_name;
  gneiss::engine::math::transform local_transform = {};
  engine::component_descriptions components{};
};

struct prefab_node_record final {
  entity_id node;
  entity_id parent;
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
      if (key.target.target_type_guid == type && key.first_property() == property)
        return true;
    return false;
  }
  [[nodiscard]] bool has_overrides(engine::guid type) const noexcept {
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
  gneiss::engine::guid uuid;
  gneiss::engine::object_address parent;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

/** 场景编辑会话：装配纯文档、运行投影、选择及保存协调。
 * 借用 Application，绑定一个运行时场景实例；close 负责卸载该实例。
 * 文档版本持有编辑内容；World 是投影，保存不读取运行时属性。
 * 不拥有 World 或第二棵运行时场景树；须先于 Application 关闭。
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
  [[nodiscard]] gneiss::engine::error adopt(runtime::application& application,
                                            gneiss::engine::world* world,
                                            gneiss::scene_instance_id scene,
                                            const scene_document_snapshot& initial,
                                            std::span<const engine::scene_object_binding> objects);
  [[nodiscard]] gneiss::engine::error create_empty(runtime::application& application,
                                                   gneiss::engine::world* world,
                                                   gneiss::engine::guid scene_uuid);
  [[nodiscard]] gneiss::engine::error create_empty(runtime::application& application,
                                                   gneiss::engine::world* world);
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  /** 使用已准备资源创建节点；成功后更新选择和脏状态，不加载资产。 */
  [[nodiscard]] engine::result<entity_id> create_mesh_node(const mesh_node_creation& description,
                                                           const scene_mesh_binding& resources);
  /** 捕获当前文档节点的资源强引用；文档关闭或应用停止后拒绝调用。 */
  [[nodiscard]] engine::result<scene_mesh_binding> capture_mesh_binding(entity_id node) const;
  [[nodiscard]] gneiss::scene_instance_id scene_handle() const noexcept { return scene_; }
  /** 替换已准备资源；当前绑定不匹配 expected 时拒绝，成功后标脏，不加载资产。 */
  [[nodiscard]] engine::error replace_mesh_binding(entity_id node,
                                                   const scene_mesh_binding& expected,
                                                   const scene_mesh_binding& replacement);
  [[nodiscard]] bool is_dirty() const noexcept { return document_ && document_->is_dirty(); }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] engine::result<scene_node_record> query_node(entity_id node) const;
  [[nodiscard]] engine::result<prefab_node_record> query_prefab_node(entity_id node) const;
  /** 从 Scene 捕获本次查询的独立值，不在文档内缓存。 */
  [[nodiscard]] engine::result<scene_query_snapshot> capture_nodes() const;
  [[nodiscard]] entity_id selection() const noexcept { return selection_; }
  [[nodiscard]] engine::result<scene_node_record> selected_node() const;
  [[nodiscard]] engine::result<prefab_node_record> selected_prefab_node() const;
  [[nodiscard]] engine::result<prefab_node_record>
  find_prefab_root(gneiss::engine::guid instance_uuid) const;
  [[nodiscard]] engine::result<prefab_node_record>
  find_prefab_source(gneiss::engine::guid instance_uuid, gneiss::engine::guid source_uuid) const;
  [[nodiscard]] engine::result<scene_node_record> find_node(gneiss::engine::guid uuid) const;

  [[nodiscard]] gneiss::engine::error select(entity_id node);
  /** 清除失效选择并返回成功；文档或应用已失效时返回错误。 */
  [[nodiscard]] gneiss::engine::error validate_selection();
  [[nodiscard]] gneiss::engine::error create_node(std::string_view name, entity_id parent,
                                                  entity_id& out_node);
  [[nodiscard]] gneiss::engine::error rename_node(entity_id node, std::string_view name);
  [[nodiscard]] gneiss::engine::error reparent_node(entity_id node, entity_id parent);
  [[nodiscard]] gneiss::engine::error
  get_world_transform(entity_id node, gneiss::engine::math::transform& output) const;
  /** 设置节点局部 TRS；成功后标记场景已修改。 */
  [[nodiscard]] gneiss::engine::error
  set_local_transform(entity_id node, const gneiss::engine::math::transform& value);
  /** 恢复 Prefab 来源节点的单个 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::error
  restore_prefab_transform_field(entity_id node, std::uint32_t field_id,
                                 gneiss::engine::math::transform& out_previous);
  /** 恢复 Prefab 来源节点的全部 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::error
  restore_prefab_transform(entity_id node, gneiss::engine::math::transform& out_previous);
  [[nodiscard]] gneiss::engine::error destroy_subtree(entity_id node,
                                                      scene_subtree_snapshot& out_snapshot);
  [[nodiscard]] gneiss::engine::error restore_subtree(const scene_subtree_snapshot& snapshot,
                                                      entity_id& out_node);
  [[nodiscard]] gneiss::engine::error duplicate_subtree(entity_id node, entity_id parent,
                                                        entity_id& out_node);
  [[nodiscard]] gneiss::engine::error set_camera(entity_id node,
                                                 const gneiss::engine::camera_description& desc);
  [[nodiscard]] gneiss::engine::error remove_camera(entity_id node);
  [[nodiscard]] gneiss::engine::error remove_mesh_renderer(entity_id node);
  [[nodiscard]] gneiss::engine::error set_mesh_renderer(entity_id node, std::string_view mesh_uri,
                                                        std::string_view material_uri);
  [[nodiscard]] gneiss::engine::error destroy_node(entity_id node);
  /** 保存回执独立于文档生命周期；完成只写回执，宿主推进后显式接收。 */
  struct save_receipt {
    gneiss::scene_instance_id scene;
    scene_document::version version;
    std::string uri, content;
    engine::error status{engine::error::not_ready};
    bool complete{};
  };
  /** 提交当前内容的单文件安全保存；不阻塞、不修改 URI 或脏状态。 */
  [[nodiscard]] engine::error begin_save(gneiss::engine::files::service& files,
                                         std::string_view uri,
                                         gneiss::engine::files::write_mode mode,
                                         std::shared_ptr<save_receipt>& output);
  /** 接收成功保存；场景实例不匹配时拒绝，保存期间的新修改保持 dirty。 */
  [[nodiscard]] engine::error accept_save(const save_receipt& receipt);
  [[nodiscard]] engine::result<engine::component_descriptions> components(entity_id node) const;
  [[nodiscard]] engine::error set_component_description(entity_id node,
                                                        const engine::component_description&);
  [[nodiscard]] engine::error add_component_description(entity_id,
                                                        const engine::component_description&);
  [[nodiscard]] engine::error remove_component_description(entity_id, engine::guid);
  [[nodiscard]] engine::result<prefab_component_state> capture_component_override(entity_id,
                                                                                  engine::guid);
  [[nodiscard]] engine::error set_component_override(entity_id, engine::guid,
                                                     const prefab_component_state&);
  [[nodiscard]] engine::result<std::string> serialize() const;
  /** 当前编辑内容；不从 World 反向捕获。 */
  [[nodiscard]] engine::result<scene_document_snapshot> capture_content() const;
  /** 接受合法内容后尝试同步；返回值表示内容是否接受，同步错误单独查询。 */
  [[nodiscard]] engine::error accept_content(const scene_document_snapshot&);
  [[nodiscard]] engine::error synchronize();
  [[nodiscard]] engine::error projection_status() const noexcept;
  using version = scene_document::version;
  [[nodiscard]] version current_version() const noexcept {
    return edit_base_ ? edit_base_->current() : document_ ? document_->current() : version{};
  }
  /** 历史切换只接受当前基线；投影失败保留目标版本并由 projection_status 报告。 */
  [[nodiscard]] engine::error restore_changes(const scene_document::revision& expected,
                                              const scene_document::revision& target,
                                              std::span<const document_patch>, bool undo);
  [[nodiscard]] engine::error restore_version(const version& expected, const version& target);
  [[nodiscard]] engine::result<scene_subtree_snapshot> capture_subtree(entity_id) const;
  [[nodiscard]] engine::result<prefab_header_snapshot> capture_prefab_header(engine::guid) const;
  [[nodiscard]] engine::error apply_prefab(const scene_subtree_snapshot&, bool removing,
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
  friend engine::error execute_scene_edit(editor_command_history&, scene_edit_session&, std::string,
                                          const std::function<engine::error()>&, std::string);
  // 复合编辑期间借用权威文档，document_ 临时指向独立候选。
  const scene_document* edit_base_{};
  using content_edit =
      std::function<engine::error(engine::scene_description&, scene_document_snapshot&)>;
  engine::error edit_content(const content_edit&, const std::function<engine::error()>& = {});
  struct edit_target {
    engine::guid object, source;
    engine::game_object_description effective;
  };
  engine::result<edit_target> locate(entity_id) const;
  engine::error edit_component(entity_id, engine::guid, const engine::component_description&,
                               int mode);
  std::unique_ptr<scene_document> document_;
  scene_projection projection_;
  engine::error encode_scene(std::string& output) const;

  gneiss::engine::world* world_ = nullptr;
  // 借用宿主；宿主必须在关闭/销毁 Application 前关闭编辑文档。
  engine::error report_projection(const version&, engine::error) noexcept;
  runtime::application* application_{};
  gneiss::scene_instance_id scene_{};
  entity_id selection_;
  std::string uri_;
};

} // namespace gneiss::editor

#endif
