<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-148：Runtime 资源事务重载实施记录

## 结果

Runtime 已在主线程帧安全点接收 Asset 域的 `reload` 与 `resync` 命令，并按会话维护最后成功应用的
修订号。旧修订不会重复执行；新会话会重新建立修订基线。每个请求都会返回已应用、失败、旧修订
或需要重启的事务结果。

资源缓存新增批量重载事务。事务先构造并验证全部候选资源，再一次替换缓存映射；任一候选失败时，
缓存保持原状。替换前取得的资源租约继续持有旧对象，最后一个租约释放后旧对象才被销毁，因此不
产生悬空资源。

## 边界

本里程碑建立修订协调、主线程安全点和缓存原子提交语义。Runtime 当前尚未接入 Texture、Material
和静态 Mesh 的具体候选资源加载器，因此会对真实资产请求返回“需要重启”。三类资源的依赖排序、
后端对象创建和场景实例刷新由 M-149 完成。

本阶段没有发现必须下沉至 Granit 的通用能力，也没有修改 Granit。

## 验证

- Windows Clang Shared Debug：Asset 协议、Runtime IPC 会话、Runtime IPC 命令、Runtime 资产
  重载器、Runtime 冒烟和资源服务共 6 项测试通过。
- Windows Clang Static Debug：相同 6 项测试通过。
- 资源缓存测试覆盖多资源成功提交、候选加载失败回滚和旧租约继续有效。
- 修订协调测试覆盖成功、旧修订、失败重试、需要重启、新会话及非主线程拒绝。
