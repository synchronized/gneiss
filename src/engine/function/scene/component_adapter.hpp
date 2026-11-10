// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/render_resource.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/object_address.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <functional>
#include <optional>
namespace gneiss::engine {
class asset_manager;
/** 当前支持的编辑入口；资源绑定操作由编辑器异步资产命令执行。 */
enum class component_editor_route { reflected, mesh_assets };
/** 借用所属线程 World 和已准备的资源；回调不得保留上下文。 */
struct component_context {
  game_object target;
  /** 提供时为固定资源来源，不回退到当前缓存；仅在本次调用期间借用。 */
  std::function<result<render_resource_ref>(const render_resource_source&)> lookup_resource{};
  asset_manager* resources{};
  /** 仅解析本次实例的来源地址；不搜索其他实例。 */
  std::function<result<game_object>(object_address)> lookup_object{};
};
/** 可选编辑能力；与运行安装契约独立，缺失操作由编辑入口返回 unsupported。 */
struct component_editor_adapter {
  /** 可选属性身份；未指定时使用组件描述类型。 */
  guid property_type;
  std::function<result<value>(const component_description&, guid, meta::property_id)> read;
  std::function<result<component_description>(const component_description&, guid, meta::property_id,
                                              const value&)>
      edit;
  std::function<result<component_description>(const component_description&,
                                              const std::function<error(meta::instance)>&)>
      modify;
  std::function<result<component_description>()> create_default;
  /** 仅控制编辑器删除入口，不限制运行卸载、同步和组件移除。 */
  bool removable = false;
  component_editor_route editor_route = component_editor_route::reflected;
  /** 编辑采用/历史重放前保留运行状态；普通安装和加载不调用此策略。 */
  std::function<result<component_description>(game_object, const component_description&)>
      prepare_editor;
};
/** 组件实现契约：安装失败不修改目标组件；捕获不修改 World。
 * 安装必须允许用之前捕获的有效描述恢复；不执行 I/O 或阻塞等待。 */
struct component_adapter {
  guid type;
  std::function<error(const component_description&)> validate;
  std::function<error(component_context&, const component_description&)> install;
  /** 已有组件的编辑更新，保留未在描述中表达的运行状态；失败不修改组件。
   * 未提供时使用 install，适用于没有额外运行状态的组件。 */
  std::function<error(component_context&, const component_description&)> update;
  std::function<result<std::optional<component_description>>(game_object)> capture;
  std::function<error(game_object)> remove;
  std::function<bool(const component_description&, const component_description&)> equal;
  std::function<std::vector<render_resource_source>(const component_description&)> dependencies;
  /** 全部组件安装后调用；不得修改其他对象，缺失必需引用返回错误。 */
  std::function<error(component_context&, const component_description&)> resolve_references;
  component_editor_adapter editor{};
};
/** 启动期注册；重复 GUID 拒绝，首次查询后冻结。纯描述回调须支持并发只读调用。
 * validate/install/capture/remove/equal 为必需；编辑回调可省略，对应操作返回 unsupported。
 * 无 dependencies 回调表示没有外部资源依赖。 */
GNEISS_API error register_scene_component(component_adapter adapter);
/** 更新已有组件，保持组件 UUID；不存在时返回 not_found，不隐式创建。 */
GNEISS_API error update_component(component_context, const component_description&);
/** 类型化编辑扩展；仅显式提供时生成反射属性操作。 */
template <class T> struct component_editor_operations {
  guid property_type = type_identity<T>::guid();
  result<value> (*read)(const T&, guid, meta::property_id){};
  error (*edit)(T&, guid, meta::property_id, const value&){};
  result<T> (*create_default)(){};
  bool removable = false;
  component_editor_route editor_route = component_editor_route::reflected;
  result<T> (*prepare_editor)(game_object, const T&){};
};
template <class T> struct component_operations {
  error (*validate)(const T&){};
  error (*install)(component_context&, const T&){};
  error (*update)(component_context&, const T&){};
  result<std::optional<T>> (*capture)(game_object){};
  error (*remove)(game_object){};
  bool (*equal)(const T&, const T&){};
  std::vector<render_resource_source> (*dependencies)(const T&){};
  error (*resolve_references)(component_context&, const T&){};
  std::optional<component_editor_operations<T>> editor{};
};

template <class T> component_adapter make_component_adapter(component_operations<T> ops) {
  if (!ops.validate || !ops.install || !ops.capture || !ops.remove || !ops.equal)
    return {};
  component_adapter registration{
      .type = type_identity<T>::guid(),
      .validate =
          [ops](const component_description& value) {
            const auto* typed = value ? value->get<T>() : nullptr;
            return typed ? ops.validate(*typed) : error{error::invalid_argument};
          },
      .install =
          [ops](component_context& context, const component_description& value) {
            const auto* typed = value ? value->get<T>() : nullptr;
            return typed ? ops.install(context, *typed) : error{error::invalid_argument};
          },
      .update =
          [ops](component_context& context, const component_description& value) {
            const auto* typed = value ? value->get<T>() : nullptr;
            if (!typed)
              return error{error::invalid_argument};
            return ops.update ? ops.update(context, *typed) : ops.install(context, *typed);
          },
      .capture = [ops](game_object target) -> result<std::optional<component_description>> {
        auto value = ops.capture(target);
        if (!value)
          return value.error();
        if (!*value)
          return std::optional<component_description>{};
        return std::optional<component_description>{make_component_description(std::move(**value))};
      },
      .remove = ops.remove,
      .equal =
          [ops](const component_description& a, const component_description& b) {
            return a && b && a->get<T>() && b->get<T>() && ops.equal(*a->get<T>(), *b->get<T>());
          },
      .dependencies =
          [ops](const component_description& value) {
            return ops.dependencies ? ops.dependencies(*value->get<T>())
                                    : std::vector<render_resource_source>{};
          },
      .resolve_references =
          [ops](component_context& context, const component_description& value) {
            const auto* typed = value ? value->get<T>() : nullptr;
            if (!typed)
              return error{error::invalid_argument};
            return ops.resolve_references ? ops.resolve_references(context, *typed)
                                          : error{error::success};
          }};
  if (!ops.editor)
    return registration;
  registration.editor = {
      .property_type = ops.editor->property_type,
      .read = [ops](const component_description& source, guid type,
                    meta::property_id property) -> result<value> {
        const auto* typed = source ? source->get<T>() : nullptr;
        if (!typed)
          return error::invalid_argument;
        if (ops.editor->read)
          return ops.editor->read(*typed, type, property);
        auto root = meta::view(std::as_const(*typed));
        auto field = root ? root->member(property) : result<meta::const_instance>{root.error()};
        return field ? field->read_value() : result<value>{field.error()};
      },
      .edit = [ops](const component_description& source, guid type, meta::property_id property,
                    const value& input) -> result<component_description> {
        if (!source || !source->get<T>())
          return error::invalid_argument;
        auto copy = *source->get<T>();
        auto status = [&]() -> error {
          if (ops.editor->edit)
            return ops.editor->edit(copy, type, property, input);
          auto root = meta::view(copy);
          if (!root)
            return root.error();
          auto field = root->member(property);
          return field ? field->assign(input) : field.error();
        }();
        if (status.ok())
          status = ops.validate(copy);
        if (status.failed())
          return status;
        return make_component_description(std::move(copy));
      },
      .modify = [ops](const component_description& source,
                      const std::function<error(meta::instance)>& change)
          -> result<component_description> {
        if (!source || !source->get<T>())
          return error::invalid_argument;
        auto copy = *source->get<T>();
        auto view = meta::view(copy);
        if (!view)
          return view.error();
        auto status = change(*view);
        if (status.ok())
          status = ops.validate(copy);
        if (status.failed())
          return status;
        return make_component_description(std::move(copy));
      },
      .create_default =
          ops.editor->create_default
              ? std::function<
                    result<component_description>()>{[ops]() -> result<component_description> {
                  auto source = ops.editor->create_default();
                  if (!source)
                    return source.error();
                  auto status = ops.validate(*source);
                  if (status.failed())
                    return status;
                  return make_component_description(std::move(*source));
                }}
              : std::function<result<component_description>()>{},
      .removable = ops.editor->removable,
      .editor_route = ops.editor->editor_route,
      .prepare_editor = [ops](game_object target,
                              const component_description& value) -> result<component_description> {
        if (!value || !value->get<T>())
          return error::invalid_argument;
        if (!ops.editor->prepare_editor)
          return value;
        auto prepared = ops.editor->prepare_editor(target, *value->get<T>());
        return prepared
                   ? result<component_description>{make_component_description(std::move(*prepared))}
                   : result<component_description>{prepared.error()};
      }};
  return registration;
}
template <class T> error register_scene_component(component_operations<T> ops) {
  return register_scene_component(make_component_adapter(ops));
}
GNEISS_API result<component_description>
modify_component(const component_description&, const std::function<error(meta::instance)>&);
struct component_editor_info {
  guid type;
  bool addable = false;
  /** 仅控制编辑器删除入口，不限制运行卸载、同步和组件移除。 */
  bool removable = false;
  component_editor_route route = component_editor_route::reflected;
};
/** 未注册身份原样返回；已注册运行属性身份统一映射到描述身份。 */
GNEISS_API guid component_description_type(guid property_type) noexcept;
GNEISS_API result<component_editor_info> component_editor(guid type) noexcept;
GNEISS_API result<component_description>
prepare_component_editor_value(world&, scene_node_id, const component_description&);
/** 返回冻结注册表的自有摘要；显示名称仍从 Meta 查询。 */
GNEISS_API result<std::vector<component_editor_info>> component_editor_types();
GNEISS_API result<component_description> default_component_description(guid type);
/** 只处理单个组件；重复添加返回 invalid_state，缺失删除返回 not_found。 */
GNEISS_API error add_component(component_context, const component_description&, guid identity = {});
GNEISS_API error remove_component(component_context, guid type);
GNEISS_API error resolve_component_references(component_context, const component_descriptions&);
GNEISS_API error validate_components(const component_descriptions&);
GNEISS_API result<component_descriptions> capture_components(world&, scene_node_id);
GNEISS_API error install_components(component_context, const component_descriptions&);
/** 同步替换组件集合；失败由调用方用捕获快照恢复，不改变节点身份。 */
GNEISS_API error replace_components(component_context, const component_descriptions&);
GNEISS_API bool equal_components(const component_descriptions&, const component_descriptions&);
GNEISS_API result<std::vector<render_resource_source>>
component_dependencies(const component_descriptions&);
GNEISS_API error edit_component(component_descriptions&, guid, meta::property_id, const value&);
GNEISS_API result<value> read_runtime_component(world&, scene_node_id, guid, meta::property_id);
GNEISS_API error edit_runtime_component(component_context, guid, meta::property_id, const value&);
/** 在 World 所属线程定位节点组件；读出副本后访问，写入成功才提交。不会保留对象指针。
 * 失败保持既有组件；普通组件使用已注册适配器；未知组件类型返回
 * unsupported。值及字符串由调用方持有。 */
[[nodiscard]] GNEISS_API result<value> read_component_property(world& world, std::uint64_t entity,
                                                               guid, meta::property_id);
[[nodiscard]] GNEISS_API error write_component_property(world& world, std::uint64_t entity, guid,
                                                        meta::property_id, const value&);
}
