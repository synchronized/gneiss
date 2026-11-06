// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/asset/asset_loading.hpp"
#include "engine/function/asset/asset_request.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine {
class resource_cache;
class task_executor;
class render_service;
struct scene_instantiation_data;
class progress_notification;
struct mesh_view;
struct texture_view;

namespace files {
class service;
}
namespace detail {
struct asset_construction;
}
}

namespace gneiss::engine {
/** 应用级资产管理器。拥有 CPU 数据索引，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 统一拥有描述数据和运行资源；Render 仅管理后端 RID。 */
class asset_manager final {
public:
  /** 异步准备结构依赖、层级顺序和视觉依赖清单；不创建 World，不等待视觉资源。 */
  GNEISS_API result<asset_request<scene_instantiation_data>>
  prepare_scene(std::string_view uri, bool reload = false) noexcept;
  GNEISS_API result<asset_request<scene_instantiation_data>>
  prepare_prefab(std::string_view uri, bool reload = false) noexcept;
  /** 绑定渲染服务；Render 必须覆盖本服务的运行与关闭阶段。 */
  GNEISS_API error attach_render(render_service& render) noexcept;
  /** 配置 CPU 加载；首次配置后保留执行器与 profile，停止后拒绝重新装配。 */
  GNEISS_API error configure(task_executor& executor, texture_prepare_profile profile,
                             std::shared_ptr<progress_notification> notification = {}) noexcept;
  GNEISS_API result<render_resource_ref> reserve_resource(const render_resource_source&) noexcept;
  /** 只查找 CPU 内容已就绪的缓存，不读取文件、不等待 GPU。 */
  GNEISS_API result<render_resource_ref> find_loaded(const render_resource_source&,
                                                     texture_prepare_profile = {}) const noexcept;
  GNEISS_API bool owns(const ref<const engine::mesh>&) const noexcept;
  GNEISS_API bool owns(const ref<const engine::texture>&) const noexcept;
  GNEISS_API bool owns(const ref<const engine::material>&) const noexcept;
  GNEISS_API bool owns(const render_resource_ref&) const noexcept;
  /** 所属线程创建程序化资源并返回强引用；失败返回错误，不接管外部 RID。 */
  GNEISS_API result<ref<const engine::texture>> create_texture(const texture_view&) noexcept;
  GNEISS_API result<ref<const engine::mesh>> create_mesh(const mesh_view&) noexcept;
  GNEISS_API result<ref<const engine::material>>
  create_material(const material_parameters&, std::array<ref<const engine::texture>, 5>) noexcept;
  GNEISS_API result<ref<const engine::texture>> reserve_texture() noexcept;
  GNEISS_API result<ref<const engine::mesh>> reserve_mesh() noexcept;
  GNEISS_API result<ref<const engine::material>> reserve_material() noexcept;
  /** 仅锁定本管理器已创建的对象；未知或已释放 RID 返回空。 */
  GNEISS_API ref<const engine::texture> find_texture(rid) const noexcept;
  GNEISS_API ref<const engine::mesh> find_mesh(rid) const noexcept;
  GNEISS_API ref<const engine::material> find_material(rid) const noexcept;
  /** 单资源异步加载；默认复用缓存，同身份的在途请求共享工作（包括 reload）。
   * 输入在返回前复制；提交失败返回错误。每项独立完成，不等待 GPU。 */
  GNEISS_API result<asset_request<render_resource_ref>> load(const render_resource_source&,
                                                             bool reload = false);

  GNEISS_API texture_prepare_profile profile() const noexcept;

  /** 注入同线程文件服务；宿主驱动文件完成分发，advance 只推进 CPU 解码和资源结果。
   * 不提供同步文件读取；缓存查询使用 find_loaded，服务可由宿主共享持有。
   * 使用默认注册表，宿主须先 initialize_meta。 */
  explicit GNEISS_API asset_manager(std::shared_ptr<engine::files::service> files);
  GNEISS_API ~asset_manager();
  /** 借用宿主解码执行器；文件 I/O 由服务后端执行，不创建独立线程池。 */
  GNEISS_API error configure(task_executor& executor) noexcept;
  /** 宿主每次先分发文件完成，再推进本管理器；不驱动文件或其他服务回调。 */
  GNEISS_API void advance();
  /** 请求停止：立即拒绝新加载、终止观察者并取消 I/O/CPU 任务，不等待任务退出。
   * 宿主持有文件服务并继续分发，调用 advance 收尾，直到 stopped。
   * 不自动关闭共享文件服务；已完成资源引用不失效。 */
  GNEISS_API void request_stop() noexcept;
  [[nodiscard]] GNEISS_API bool stopped() const noexcept;
  /** 迁移期同步关闭兜底：终止观察者并取消文件请求，等待已有 CPU 解码任务收尾。
   * 共享文件服务继续由宿主分发/关闭；独占服务随管理器销毁放弃通知，后端保活实际 I/O。 */
  GNEISS_API void stop() noexcept;
  /** 显式注册 CPU 类型；首次请求后冻结注册。来源身份由注入的文件服务确定。 */
  [[nodiscard]] GNEISS_API engine::error
  register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept;
  /** 无需继承或注册反射；注册失败不改变已有解码器，首次加载后禁止注册；内置 mesh_data/texture_data
   * 及 scene/prefab 资产使用专用 Loader，不接受覆盖。 */
  template <class T>
  [[nodiscard]] error register_decoder(asset_decode_function<T> function) noexcept {
    if (!function)
      return error::invalid_argument;
    try {
      return register_decoder(std::make_shared<detail::typed_asset_decoder<T>>(function));
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }
  /** 默认复用已完成缓存；reload 重新读取并解析，在途同键请求合并。
   * Scene/Prefab 文件须请求 scene/prefab 对象，描述类型返回 unsupported。
   * 所属线程调用；执行器须存活至 stop 返回。 */
  template <typename T>
  [[nodiscard]] result<asset_request<T>> load(std::string_view uri,
                                              asset_load_options options) noexcept {
    if constexpr (render_resource<T>) {
      try {
        if (options.texture_profile != texture_prepare_profile{} &&
            options.texture_profile != profile())
          return error::invalid_argument;
        auto requested = load(render_resource_source{std::string(uri), resource_traits<T>::kind},
                              options.reload);
        if (!requested)
          return requested.error();
        return asset_request<T>{*requested};
      } catch (const std::bad_alloc&) {
        return error::out_of_memory;
      } catch (...) {
        return error::internal;
      }
    }
    std::shared_ptr<detail::asset_state> state;
    const auto status =
        request(runtime_type_of<T>(), uri, options, state, &detail::make_reflected_decoder<T>);
    if (status.failed())
      return status;
    return asset_request<T>{std::move(state)};
  }
  template <typename T>
  [[nodiscard]] result<asset_request<T>> load(std::string_view uri, bool reload = false) noexcept {
    asset_load_options options{.reload = reload};
    return load<T>(uri, options);
  }
  /** 只查询已完成缓存，不读取文件或提交任务；未命中返回 not_found。 */
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> find_loaded(std::string_view uri,
                                                       asset_load_options options) const noexcept {
    if constexpr (render_resource<T>) {
      if (options.texture_profile != texture_prepare_profile{} &&
          options.texture_profile != profile())
        return error::not_found;
      try {
        auto found = find_loaded(render_resource_source{std::string(uri), resource_traits<T>::kind},
                                 profile());
        if (!found)
          return found.error();
        return resource_cast<T>(*found);
      } catch (const std::bad_alloc&) {
        return error::out_of_memory;
      } catch (...) {
        return error::internal;
      }
    }
    std::shared_ptr<const void> value;
    const auto status = find_loaded_erased(runtime_type_of<T>(), uri, value, options);
    if (status.failed())
      return status;
    return asset_ref<const T>{std::static_pointer_cast<const T>(value)};
  }
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> find_loaded(std::string_view uri) const noexcept {
    asset_load_options options;
    return find_loaded<T>(uri, options);
  }
  /** 构造不带 URI 的不可变 CPU 资产；不登记缓存。 */
  template <class T>
    requires(!render_resource<T>)
  [[nodiscard]] result<asset_ref<const T>> create(T value) noexcept try {
    return asset_ref<const T>{std::make_shared<const T>(std::move(value))};
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  /** 所属线程采用已经构造的数据；成功后较早异步请求不能覆盖缓存。 */
  template <class T>
    requires(!render_resource<T>)
  [[nodiscard]] result<asset_ref<const T>>
  store(std::string_view uri, std::shared_ptr<const T> value, asset_load_options options) noexcept {
    std::shared_ptr<const void> stored;
    const auto status = store_erased(runtime_type_of<T>(), uri, std::move(value), stored, options);
    if (status.failed())
      return status;
    return asset_ref<const T>{std::static_pointer_cast<const T>(stored)};
  }
  template <class T>
    requires(!render_resource<T>)
  [[nodiscard]] result<asset_ref<const T>> store(std::string_view uri,
                                                 std::shared_ptr<const T> value) noexcept {
    asset_load_options options;
    return store<T>(uri, std::move(value), options);
  }
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  /** 借用同线程异步文件服务；停止后为空。 */
  [[nodiscard]] GNEISS_API std::shared_ptr<engine::files::service> async_files() const noexcept;
  /** 解析统一来源身份，不执行文件 I/O。 */
  [[nodiscard]] GNEISS_API result<asset_identity> identify(std::string_view uri,
                                                           std::uint32_t type) const noexcept;
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  GNEISS_API void collect_unused() noexcept;
  /** 当前弱索引键数，包含尚未清理的过期项；不暴露缓存写入口。 */
  [[nodiscard]] GNEISS_API std::size_t cached_count() const noexcept;

private:
  GNEISS_API result<asset_request<scene_instantiation_data>>
  prepare_structure(std::string_view uri, bool prefab, bool reload) noexcept;
  friend struct detail::asset_construction;
  std::unique_ptr<detail::asset_construction> impl_;
  resource_cache& cache() noexcept;
  const resource_cache& cache() const noexcept;
  bool stop_requested() const noexcept;
  bool loading_configured() const noexcept;
  void notify_progress() const noexcept;
  /** 任务中间数据只由任务保活，不登记为最终资产。 */
  template <class T>
  result<asset_request<T>> decode_data(std::string_view uri, asset_load_options options) noexcept {
    std::shared_ptr<detail::asset_state> state;
    const auto status = request(runtime_type_of<T>(), uri, options, state,
                                &detail::make_reflected_decoder<T>, false);
    if (status.failed())
      return status;
    return asset_request<T>{std::move(state)};
  }
  using decoder_factory = result<std::shared_ptr<const asset_decoder>> (*)();
  [[nodiscard]] GNEISS_API error resolve_decoder(type_id, decoder_factory,
                                                 std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] GNEISS_API engine::error request(type_id type, std::string_view uri,
                                                 asset_load_options options,
                                                 std::shared_ptr<detail::asset_state>& output,
                                                 decoder_factory fallback,
                                                 bool cache_result = true) noexcept;
  GNEISS_API error find_loaded_erased(type_id, std::string_view, std::shared_ptr<const void>&,
                                      asset_load_options) const noexcept;
  GNEISS_API error store_erased(type_id, std::string_view, std::shared_ptr<const void>,
                                std::shared_ptr<const void>&, asset_load_options) noexcept;
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
};
}

#endif
