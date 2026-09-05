<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-176：Granit 运行时资产边界与接入审计

## 结果

审计确认 Granit 0.10 的 Material、Render Pipeline、Environment Map 和 Timestamp Query 公共接口
足以支撑 Gneiss 后续接入。Gneiss 当前没有使用参考 Render Pipeline，而是以内嵌 Shader 和显式
Graphics Pipeline 绘制，因此标准 PBR 接入属于后端投影迁移，不能只替换两个 Shader 文件。

[ADR-039：Granit 渲染资产只作为后端投影](../decisions/ADR-039-granit-render-asset-projection.md)
已经固定源资产、作者数据、Runtime 资源快照、Granit 投影和 Frame Packet 的职责边界。

## 接口审计

- `granit_material` 从 Material 归档创建，使用调用方 Shader Resolver 按稳定内容 ID 获取
  `.grshader`；Resolver 存储必须至少存活到 Material 销毁。
- Granit 标准 PBR 固定 Frame、Material、Object、Lighting 四组 Binding，并随 RenderPipeline
  component 安装 Shader 资产；Gneiss 不复制这些 Binding 定义。
- `granit_environment_map` 可从调用期间借用的 `GRENV` 字节或内建中性环境创建，返回供参考管线
  借用的 IBL 视图、推荐曝光和环境参数。
- Render Pipeline 提供 Material、Mesh、Scene、Draw Binding、Canvas、Debug Draw、Environment 和
  GPU Metrics 组合入口，但对象仍必须在同一 Renderer 与渲染线程内串行使用。
- Granit 不负责 Gneiss 的 VFS、资产数据库、导入缓存、异步加载、RID 或热重载事务。

## Provider 与分发结论

- PACKAGE Provider 通过 `granit_RENDER_PIPELINE_ASSET_DIR` 暴露已安装公共资产目录。
- FETCH Provider 可从锁定 Granit 源目录取得同一资产，但 Gneiss 必须将所需产物暂存到自己的构建和
  安装数据目录，Runtime 不能记录该源码路径。
- 仅由父工程提供 Granit targets 时，0.10.0 没有统一保证 build-tree
  `granit_RENDER_PIPELINE_ASSET_DIR` 可用；Gneiss 接入前应提供显式资产目录覆盖，并建议 Granit
  统一 PACKAGE 与 build-tree 的资产目录契约。

## 后续实施约束

- M-177 可在当前显式 Pipeline 上使用底层 Timestamp Query，不依赖 PBR 迁移。
- M-178 先建立标准资产暂存与 Resolver，再事务式替换现有后端投影，保留 UI 与 Debug Draw 行为。
- M-179 复用不可丢弃 Command 上传 Environment，加载失败不改变活动环境。
- 当前不需要修改 Granit 代码；父工程资产目录契约作为建议 PR 单独交由用户决定。
