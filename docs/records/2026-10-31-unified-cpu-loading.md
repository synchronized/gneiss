<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 统一 CPU 加载实施记录

日期：2026-10-31。对应 [PLAN-094](../plans/PLAN-094-unified-asset-loading.md)。

## 变更

删除一次性数据请求分支，网格、纹理与普通描述共享 asset_request<T>。
类型步骤迁入 detail/asset_loader.hpp；缓存增加完整参数键和弱引用策略。
resource_service 将共享 CPU 数据直接交给 Render；纹理准备不复制负载。
查询与 store 同步支持加载选项。当前接口见[资产参考](../reference/assets.md)。
原工作区 asset_manager.cpp 的错误判断与控制流整理保留于迁移中。

## 验证

- cmake --build --preset windows-clang-debug -j6：通过，开发警告作为错误。
- 10 项 CPU CTest 通过：assets.manager、assets.material、resource-retirement-queue、
  resource_service、mesh_binary、texture_ktx2、texture_binary、render_asset_preparation、
  render_asset_loader、texture-load-service（均为 gneiss 前缀）。
- 新增同键合并、单观察者取消、弱缓存失效、profile/上限隔离与纹理共享地址检查；
  texture-load-service 同时运行线程与协作执行模式。
- 源码、测试、构建配置及当前 Reference 不再引用已删除加载入口。
- git diff --check：通过。

## 限制

未验证 GPU、大场景、浏览器后端或远端 CI。内置多阶段 Loader 仍按类型选择，
不提供公共多阶段插件注册；普通类型继续支持显式解码器和反射回退。
弱缓存最后一个引用释放后，下次加载需要重新读取；Render 仍持有数据时可复用缓存。
本次只完成 PLAN-094，不代表引用持久化 PLAN-093 的其他工作已经完成。

## 后续简化：统一弱缓存

按用户要求删除 asset_cache_policy，CPU 与运行资源缓存都使用弱索引。
类型化运行资源引用保活缓存条目，避免仍有使用者时重新创建同 URI 对象。
collect_unused 仅清理过期键并消费运行资源销毁队列；不再控制 CPU 对象释放时机。
失败重载结束后，无使用者的旧数据同样释放。

Windows Clang Debug 编译通过；上述 10 项及 prefab_description、scene-instantiation
共 12 项 CPU 测试通过。增加未调用 collect_unused 时弱索引立即失效的断言，
验证普通 CPU 数据与类型化缓存引用。未运行 GPU、大场景、浏览器或远端 CI。
