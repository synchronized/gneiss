<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 文档对象与文件布局迁移

日期：2026-10-21。依据：[模块布局计划](../plans/PLAN-051-module-layout.md) 与 ADR-069。

## 交付

- editor_session 改为 documents/scene_document，CMake 目标改为 gneiss_scene_document，
  宿主成员命名为 document；不保留旧类型别名或头文件。
- 将已有命令与历史实现移入 commands，相机/网格/Gizmo 移入 viewport，
  层级/属性/资源/控制台/运行时面板移入 panels。
- 调用点、构建清单、私有头检查与依赖检查均使用新路径；不扩大包含目录来兼容旧路径。
- 原有文档绑定场景、close 卸载场景、Application 比文档长寿的约定保留；
  不复制 World 或运行时树，不新增并行状态。历史仍由宿主持有。

## 验收

Windows Clang Debug 完整构建通过。29 项定向测试全部通过：文档打开/保存、Prefab
编辑与作者操作、资产工作流、相机、网格、Gizmo、属性及各面板、内部头可见性、
依赖正向与负向检查。未运行图形窗口交互、Sponza、跨平台或远端 Actions。
公共 ABI 和安装文件集没有改变，本批没有重新运行安装消费矩阵。

## 剩余工作

Apps 的 editor_application 仍混合 UI 编排与平台/进程/IPC 装配，尚未迁入 src/editor。
文档与历史的组合所有权、在途编辑操作的协调仍需按现有取消/关闭语义逐步审查。
资源缓存收敛、CPU/GPU 租约分离等计划项不属于本批完成范围。
