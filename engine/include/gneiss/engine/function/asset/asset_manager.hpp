// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include <functional>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/asset/asset_batch_request.hpp>
#include <gneiss/engine/function/asset/asset_loading.hpp>
#include <gneiss/engine/function/asset/asset_request.hpp>
#include <gneiss/engine/resource/asset_decoder.hpp>
#include <gneiss/engine/resource/asset_identity.hpp>
#include <gneiss/engine/resource/mesh/mesh_data.hpp>
#include <gneiss/engine/resource/texture/texture_data.hpp>
#include <memory>
#include <utility>

namespace gneiss {
struct scene_content;
struct scene_description;
struct prefab_description;
class scene;
class prefab;
struct asset_loader;
struct asset_dependency;
class resource_cache;
namespace render {
class service;
}
struct mesh_view;
struct texture_view;

namespace files {
class service;
}
namespace detail {
struct asset_entry;
struct asset_construction;
struct render_load_operation;
}
}

namespace gneiss {
/** 应用级资产管理器。拥有 CPU 数据索引，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 统一拥有描述数据和运行资源；Render 仅管理后端 RID。 */
class asset_manager final {
public:
  /** 绑定渲染服务；Render 必须覆盖本服务的运行与关闭阶段。 */
  GNEISS_ENGINE_API error attach_render(gneiss::render::service& render);
  /** 配置 CPU 加载；scope 须独占且空闲；成功后接管关闭责任。省略时内部创建。
   * 首次配置后保留执行器与 profile，停止后拒绝重新装配。 */
  GNEISS_ENGINE_API error configure(gneiss::tasks::executor& executor,
                                    texture_prepare_profile profile,
                                    gneiss::tasks::scope scope = {});
  /** 只查找 CPU 内容已就绪的缓存，不读取文件、不等待 GPU。 */
  GNEISS_ENGINE_API result<render_resource_ref> find(const render_resource_source&,
                                                     texture_prepare_profile = {}) const;
  GNEISS_ENGINE_API bool owns(const ref<const gneiss::mesh>&) const;
  GNEISS_ENGINE_API bool owns(const ref<const gneiss::texture>&) const;
  GNEISS_ENGINE_API bool owns(const ref<const gneiss::material>&) const;
  GNEISS_ENGINE_API bool owns(const render_resource_ref&) const;
  /** 所属线程创建程序化资源并返回强引用；失败返回错误，不接管外部 RID。 */
  GNEISS_ENGINE_API result<ref<const gneiss::texture>> create(const texture_view&);
  GNEISS_ENGINE_API result<ref<const gneiss::mesh>> create(const mesh_view&);
  GNEISS_ENGINE_API result<ref<const gneiss::material>>
  create(const material_parameters&, std::array<ref<const gneiss::texture>, 5>);
  /** 单资源异步加载；默认复用缓存，同身份的在途请求共享工作（包括 reload）。
   * 输入在返回前复制；提交失败返回错误。每项独立完成，不等待 GPU。 */
  GNEISS_ENGINE_API result<asset_request<render_resource_ref>>
  load(const render_resource_source&, asset_load_options options = {});

  /** 独立批次观察请求；各批次可并存，取消一个不影响其他观察者。
   * 所属线程提交与领取；输入立即复制，底层同身份加载共享，关闭后请求失效。 */
  [[nodiscard]] GNEISS_ENGINE_API result<asset_batch_request>
  load_batch(std::span<const render_resource_source> sources, bool reload = false);

  GNEISS_ENGINE_API texture_prepare_profile profile() const noexcept;

  /** 注入同线程文件服务；宿主驱动文件完成分发，advance 只推进 CPU 解码和资源结果。
   * 不提供同步文件读取；缓存查询使用 find，服务可由宿主共享持有。
   * 使用默认注册表，宿主须先 initialize_meta。 */
  explicit GNEISS_ENGINE_API asset_manager(std::shared_ptr<gneiss::files::service> files);
  GNEISS_ENGINE_API ~asset_manager();
  /** 借用宿主解码执行器；文件 I/O 由服务后端执行，不创建独立线程池。 */
  GNEISS_ENGINE_API error configure(gneiss::tasks::executor& executor);
  /** 宿主每次先分发文件完成，再推进本管理器；不驱动文件或其他服务回调。 */
  GNEISS_ENGINE_API void advance();
  /** 请求停止：立即拒绝新加载、终止观察者并取消 I/O/CPU 任务，不等待任务退出。
   * 宿主持有文件服务并继续分发，调用 advance 收尾，直到 stopped。
   * 不自动关闭共享文件服务；已完成资源引用不失效。 */
  GNEISS_ENGINE_API void request_stop();
  [[nodiscard]] GNEISS_ENGINE_API bool stopped() const noexcept;
  /** 迁移期同步关闭兜底：终止观察者并取消文件请求，等待已有 CPU 解码任务收尾。
   * 共享文件服务继续由宿主分发/关闭；独占服务随管理器销毁放弃通知，后端保活实际 I/O。 */
  GNEISS_ENGINE_API void stop() noexcept;
  using loader_factory =
      std::function<std::shared_ptr<asset_loader>(asset_identity, asset_load_options)>;
  /** 注册完整加载协议；工厂在所属线程执行，decode 在工作线程执行；首次加载后冻结。 */
  /** 按进程类型加载注册资产；仅为异构引用调度使用，与模板入口共用记录。 */
  GNEISS_ENGINE_API result<asset_request<asset>> load(type_id, std::string_view,
                                                      asset_load_options = {});
  template <class T>
    requires std::derived_from<T, asset>
  error register_loader(loader_factory factory) {
    return register_loader(runtime_type_of<T>(), std::move(factory), reference_factory_for<T>());
  }
  /** 显式注册 CPU 类型；首次请求后冻结注册。来源身份由注入的文件服务确定。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error
  register_decoder(const std::shared_ptr<const asset_decoder>& loader);
  /** 无需继承或注册反射；注册失败不改变已有解码器，首次加载后禁止注册；内置 mesh_data/texture_data
   * 及 scene/prefab 资产使用专用 Loader，不接受覆盖。 */
  template <class T> [[nodiscard]] error register_decoder(asset_decode_function<T> function) {
    if (!function)
      return error::invalid_argument;
    {
      return register_decoder(std::make_shared<detail::typed_asset_decoder<T>>(function));
    }
  }
  /** 默认复用已完成缓存；reload 重新读取并解析，在途同键请求合并。
   * 仅接受 asset 派生类型；描述及普通 CPU 值返回 unsupported。
   * 所属线程调用；执行器须存活至 stop 返回。 */
  template <typename T>
  [[nodiscard]] result<asset_request<T>> load(std::string_view uri, asset_load_options options) {
    if constexpr (!std::derived_from<T, asset>)
      return error::unsupported;
    if constexpr (render_resource<T>) {
      {
        if (options.texture_profile != texture_prepare_profile{} &&
            options.texture_profile != profile())
          return error::invalid_argument;
        auto requested =
            load(render_resource_source{std::string(uri), resource_traits<T>::kind}, options);
        if (!requested)
          return requested.error();
        return asset_request<T>{*requested};
      }
    }
    std::shared_ptr<detail::asset_state> state;
    const auto status = request(runtime_type_of<T>(), uri, options, state,
                                &detail::make_reflected_decoder<T>, cache_casts<T>());
    if (status.failed())
      return status;
    return asset_request<T>{std::move(state)};
  }
  template <typename T> [[nodiscard]] result<asset_request<T>> load(std::string_view uri) {
    return load<T>(uri, asset_load_options{});
  }
  template <typename T>
  [[nodiscard]] result<asset_request<T>> reload(std::string_view uri,
                                                asset_load_options options = {}) {
    options.reload = true;
    return load<T>(uri, options);
  }
  [[nodiscard]] result<asset_request<render_resource_ref>>
  reload(const render_resource_source& source) {
    return load(source, {.reload = true});
  }
  /** 只查询资产缓存，不读取文件或提交任务；非资产返回 unsupported，未命中返回 not_found。 */
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> find(std::string_view uri,
                                                asset_load_options options) const {
    if constexpr (!std::derived_from<T, asset>)
      return error::unsupported;
    if constexpr (render_resource<T>) {
      if (options.texture_profile != texture_prepare_profile{} &&
          options.texture_profile != profile())
        return error::not_found;
      {
        auto found =
            find(render_resource_source{std::string(uri), resource_traits<T>::kind}, profile());
        if (!found)
          return found.error();
        return resource_cast<T>(*found);
      }
    }
    std::shared_ptr<const asset> value;
    const auto status = find_erased(runtime_type_of<T>(), uri, value, options);
    if (status.failed())
      return status;
    if constexpr (std::derived_from<T, asset>)
      return asset_ref<const T>{std::static_pointer_cast<const T>(value)};
    else
      return error::unsupported;
  }
  template <class T> [[nodiscard]] result<asset_ref<const T>> find(std::string_view uri) const {
    asset_load_options options;
    return find<T>(uri, options);
  }
  /** 构造匿名 CPU 资产，不登记 URI 缓存；Scene 的 Prefab 依赖须显式提供。 */
  [[nodiscard]] GNEISS_ENGINE_API result<ref<const scene>> create(scene_content);
  [[nodiscard]] GNEISS_ENGINE_API result<ref<const scene>> create(scene_description);
  [[nodiscard]] GNEISS_ENGINE_API result<ref<const prefab>> create(prefab_description);
  /** 所属线程采用已构造的非渲染资产；非资产返回 unsupported。
   * 成功后较早异步请求不能覆盖缓存。 */
  template <class T>
    requires(!render_resource<T>)
  [[nodiscard]] result<asset_ref<const T>>
  store(std::string_view uri, std::shared_ptr<const T> value, asset_load_options options) {
    if constexpr (!std::derived_from<T, asset>)
      return error::unsupported;
    if constexpr (std::derived_from<T, asset>) {
      std::shared_ptr<const asset> stored;
      const auto status =
          store_erased(runtime_type_of<T>(), uri, std::move(value), stored, options);
      if (status.failed())
        return status;
      return asset_ref<const T>{std::static_pointer_cast<const T>(stored)};
    } else
      return error::unsupported;
  }
  template <class T>
    requires(!render_resource<T>)
  [[nodiscard]] result<asset_ref<const T>> store(std::string_view uri,
                                                 std::shared_ptr<const T> value) {
    asset_load_options options;
    return store<T>(uri, std::move(value), options);
  }
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  /** 借用同线程异步文件服务；停止后为空。 */
  [[nodiscard]] GNEISS_ENGINE_API std::shared_ptr<gneiss::files::service>
  async_files() const noexcept;
  /** 解析统一来源身份，不执行文件 I/O。 */
  [[nodiscard]] GNEISS_ENGINE_API result<asset_identity> identify(std::string_view uri,
                                                                  std::uint32_t type) const;
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  GNEISS_ENGINE_API void collect_unused();
  /** 当前弱索引键数，包含尚未清理的过期项；不暴露缓存写入口。 */
  [[nodiscard]] GNEISS_ENGINE_API std::size_t cached_count() const noexcept;

private:
  using reference_factory = result<asset_request<asset>> (*)(asset_manager&, std::string_view,
                                                             asset_load_options);
  GNEISS_ENGINE_API error register_loader(type_id, loader_factory, reference_factory);
  template <class T> static reference_factory reference_factory_for() noexcept {
    return +[](asset_manager& assets, std::string_view uri,
               asset_load_options options) -> result<asset_request<asset>> {
      auto requested = assets.load<T>(uri, options);
      if (!requested)
        return requested.error();
      if constexpr (render_resource<T>)
        requested->state_->native_address = requested->template get<T>().get();
      else
        requested->state_->native_address = requested->resource().get();
      return asset_request<asset>{requested->state_};
    };
  }
  asset_diagnostic advance_dependencies(const std::shared_ptr<detail::asset_entry>&,
                                        std::span<const std::shared_ptr<asset_dependency>>);
  result<render_resource_ref> acquire_resource(const render_resource_source&);
  friend struct detail::asset_construction;
  friend struct detail::render_load_operation;
  resource_cache& cache() noexcept;
  const resource_cache& cache() const noexcept;
  bool stop_requested() const noexcept;
  bool loading_configured() const noexcept;
  /** 任务中间数据只由任务保活，不登记为最终资产。 */
  template <class T>
  result<asset_request<T>> decode_data(std::string_view uri, asset_load_options options) {
    std::shared_ptr<detail::asset_state> state;
    const auto status =
        request(runtime_type_of<T>(), uri, options, state, &detail::make_reflected_decoder<T>, {});
    if (status.failed())
      return status;
    return asset_request<T>{std::move(state)};
  }
  // 双向转换保留控制块，并正确处理 asset 基类的地址偏移。
  struct cache_conversion {
    std::shared_ptr<const asset> (*to_asset)(const std::shared_ptr<const void>&){};
    std::shared_ptr<const void> (*to_value)(const std::shared_ptr<const asset>&){};
    std::shared_ptr<const void> (*snapshot)(const std::shared_ptr<const asset>&){};
  };
  template <class T> static cache_conversion cache_casts() noexcept {
    if constexpr (std::derived_from<T, asset>) {
      return {+[](const std::shared_ptr<const void>& value) -> std::shared_ptr<const asset> {
                return std::static_pointer_cast<const T>(value);
              },
              +[](const std::shared_ptr<const asset>& value) -> std::shared_ptr<const void> {
                return std::static_pointer_cast<const T>(value);
              },
              +[](const std::shared_ptr<const asset>& value) -> std::shared_ptr<const void> {
                if constexpr (requires(const T& v) { detail::capture_asset_content(v); })
                  return detail::capture_asset_content(*std::static_pointer_cast<const T>(value))
                      .shared();
                else {
                  (void)value;
                  return {};
                }
              }};
    } else
      return {};
  }
  using decoder_factory = result<std::shared_ptr<const asset_decoder>> (*)();
  [[nodiscard]] GNEISS_ENGINE_API error resolve_decoder(type_id, decoder_factory,
                                                        std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error
  request(type_id type, std::string_view uri, asset_load_options options,
          std::shared_ptr<detail::asset_state>& output, decoder_factory fallback,
          cache_conversion conversion);
  GNEISS_ENGINE_API error find_erased(type_id, std::string_view, std::shared_ptr<const asset>&,
                                      asset_load_options) const;
  GNEISS_ENGINE_API error store_erased(type_id, std::string_view, std::shared_ptr<const asset>,
                                       std::shared_ptr<const asset>&, asset_load_options);
  detail::asset_construction* construction() noexcept;
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
}

#endif
