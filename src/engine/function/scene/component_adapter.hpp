// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_types.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <functional>
#include <optional>
namespace gneiss::engine {
class resource_service;
/** 借用所属线程 World 和已准备的资源；回调不得保留上下文。 */
struct component_context {
  world& owner;
  scene_node_id node;
  mesh_renderer renderer{};
  resource_service* resources{};
};
/** 组件实现契约：安装失败不修改目标组件；捕获不修改 World。
 * 安装必须允许用之前捕获的有效描述恢复；不执行 I/O 或阻塞等待。 */
struct component_adapter {
  guid type;
  guid property_type;
  std::function<error(const component_description&)> validate;
  std::function<error(component_context&, const component_description&)> install;
  std::function<result<std::optional<component_description>>(world&, scene_node_id)> capture;
  std::function<error(world&, scene_node_id)> remove;
  std::function<bool(const component_description&, const component_description&)> equal;
  std::function<result<component_description>(const component_description&, guid, meta::property_id,
                                              const value&)>
      edit;
  std::function<result<component_description>(const component_description&,
                                              const std::function<error(meta::mutable_instance)>&)>
      modify;
  std::function<std::vector<asset_source>(const component_description&)> dependencies;
  std::function<result<component_description>()> create_default;
  bool removable = false;
};
/** 启动期注册；重复 GUID 拒绝，首次查询后冻结。纯描述回调须支持并发只读调用。 */
GNEISS_API error register_scene_component(component_adapter adapter) noexcept;
template <class T> struct component_operations {
  error (*validate)(const T&){};
  error (*install)(component_context&, const T&){};
  result<std::optional<T>> (*capture)(world&, scene_node_id){};
  error (*remove)(world&, scene_node_id){};
  bool (*equal)(const T&, const T&){};
  error (*edit)(T&, guid, meta::property_id, const value&){};
  std::vector<asset_source> (*dependencies)(const T&){};
  guid property_type = type_identity<T>::guid();
  /** 显式选择加入编辑器；不自动假定 T{} 是可安装的描述。 */
  result<T> (*create_default)(){};
  bool removable = false;
};
template <class T> component_adapter make_component_adapter(component_operations<T> ops) {
  if (!ops.validate || !ops.install || !ops.capture || !ops.remove || !ops.equal)
    return {};
  return {.type = type_identity<T>::guid(),
          .property_type = ops.property_type,
          .validate =
              [ops](const component_description& value) {
                const auto* typed = value.get<T>();
                return typed ? ops.validate(*typed) : error{error::invalid_argument};
              },
          .install =
              [ops](component_context& context, const component_description& value) {
                const auto* typed = value.get<T>();
                return typed ? ops.install(context, *typed) : error{error::invalid_argument};
              },
          .capture = [ops](world& owner,
                           scene_node_id node) -> result<std::optional<component_description>> {
            auto value = ops.capture(owner, node);
            if (!value)
              return value.error();
            if (!*value)
              return std::optional<component_description>{};
            return std::optional<component_description>{component_description{std::move(**value)}};
          },
          .remove = ops.remove,
          .equal =
              [ops](const component_description& a, const component_description& b) {
                return a.get<T>() && b.get<T>() && ops.equal(*a.get<T>(), *b.get<T>());
              },
          .edit = [ops](const component_description& source, guid type, meta::property_id property,
                        const value& input) -> result<component_description> {
            if (!source.get<T>())
              return error::invalid_argument;
            auto copy = *source.get<T>();
            auto status = ops.edit ? ops.edit(copy, type, property, input)
                                   : meta::write_property(type, property, &copy, input);
            if (status.ok())
              status = ops.validate(copy);
            if (status.failed())
              return status;
            return component_description{std::move(copy)};
          },
          .modify = [ops](const component_description& source,
                          const std::function<error(meta::mutable_instance)>& change)
              -> result<component_description> {
            if (!source.get<T>())
              return error::invalid_argument;
            auto copy = *source.get<T>();
            auto view = meta::mutable_instance::from(copy);
            if (!view)
              return view.error();
            auto status = change(*view);
            if (status.ok())
              status = ops.validate(copy);
            if (status.failed())
              return status;
            return component_description{std::move(copy)};
          },
          .dependencies =
              [ops](const component_description& value) {
                return ops.dependencies ? ops.dependencies(*value.get<T>())
                                        : std::vector<asset_source>{};
              },
          .create_default =
              ops.create_default
                  ? std::function<
                        result<component_description>()>{[ops]() -> result<component_description> {
                      auto source = ops.create_default();
                      if (!source)
                        return source.error();
                      auto status = ops.validate(*source);
                      if (status.failed())
                        return status;
                      return component_description{std::move(*source)};
                    }}
                  : std::function<result<component_description>()>{},
          .removable = ops.removable};
}
template <class T> error register_scene_component(component_operations<T> ops) noexcept try {
  return register_scene_component(make_component_adapter(ops));
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
GNEISS_API result<component_description>
modify_component(const component_description&,
                 const std::function<error(meta::mutable_instance)>&) noexcept;
struct component_editor_info {
  guid type;
  bool addable = false;
  bool removable = false;
};
/** 返回冻结注册表的自有摘要；显示名称仍从 Meta 查询。 */
GNEISS_API result<std::vector<component_editor_info>> component_editor_types() noexcept;
GNEISS_API result<component_description> default_component_description(guid type) noexcept;
/** 只处理单个组件；重复添加返回 invalid_state，缺失删除返回 not_found。 */
GNEISS_API error add_component(component_context, const component_description&) noexcept;
GNEISS_API error remove_component(component_context, guid type) noexcept;
GNEISS_API error validate_components(const component_descriptions&) noexcept;
GNEISS_API result<component_descriptions> capture_components(world&, scene_node_id) noexcept;
GNEISS_API error install_components(component_context, const component_descriptions&) noexcept;
/** 同步替换组件集合；失败由调用方用捕获快照恢复，不改变节点身份。 */
GNEISS_API error replace_components(component_context, const component_descriptions&) noexcept;
GNEISS_API bool equal_components(const component_descriptions&, const component_descriptions&);
GNEISS_API result<std::vector<asset_source>>
component_dependencies(const component_descriptions&) noexcept;
GNEISS_API error edit_component(component_descriptions&, guid, meta::property_id,
                                const value&) noexcept;
GNEISS_API error edit_runtime_component(component_context, guid, meta::property_id,
                                        const value&) noexcept;
/** 内建渲染依赖桥接；类型判断仅在组件适配实现中。 */
GNEISS_API result<mesh_renderer> resolve_component_renderer(const component_descriptions&,
                                                            resource_service&) noexcept;
GNEISS_API mesh_renderer prepared_component_renderer(
    const component_descriptions&, const std::function<render_asset_ref(std::string_view)>&);
}
