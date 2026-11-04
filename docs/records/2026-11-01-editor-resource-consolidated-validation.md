<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-097～099 集中验收记录

日期：2026-11-01。环境：Windows、Clang Debug、仓库 `windows-clang-debug` preset。
实现基线：`ae611645`；本记录对应提交另包含一处测试契约修正，没有生产代码改动。

## 结论

相关构建通过；34 项 CPU / IPC 测试完成验收。首次运行 33 项通过、1 项失败；修正旧测试对
空材质 URI 的预期后，仅重跑受影响的 `gneiss.scene-load-preparation`，通过。未重复运行已通过项。
原始日志位于本地 `build/plans-097-099-tests.log`、`build/plans-097-099-preparation-retest.log`，
最终构建日志为 `build/plans-097-099-final-build.log`；构建目录不入库。

失败原因：场景准备测试仍将空材质引用当作非法 URI。现行组件校验与依赖枚举允许空引用，
表示无材质。本次分别验证空引用成功且不产生材质依赖，以及非空非法 URI 被拒绝。
没有放宽校验，也没有跳过测试。

## 边界核查

- `prepare_edit_assets`、`query_ready_asset`、`collect_unused_edit_assets` 在 src 中无残留。
- 网格编辑、Prefab 编辑、文档恢复及 Runtime 重载均按实际请求编号领取结果，失败/取消清理自身请求。
- Runtime 取消及进度同时关联当前请求，轮询失败结束 IPC 等待，失败修订可重试。
- 无编号的 Application / Scene 通用轮询入口仍保留，调用者为测试和基准；不在本轮删除。
- 文档恢复仅开始和提交前序列化；等待期间检查文档身份及历史占用，没有引入覆盖不全的修订号。
- 资源替换与 Prefab 覆盖的撤销重做、保存再加载由已有编辑测试覆盖；Runtime 进程测试覆盖资产
  修订发布、重试、IPC 场景加载及退出。场景 IPC 测试接收端已经显式初始化 Meta。

## 验收项目

- `gneiss.app.ipc-scene-protocol`
- `gneiss.app.ipc-asset-protocol`
- `gneiss.runtime.scene-inspection`
- `gneiss.runtime.asset-reloader`
- `gneiss.editor.prefab-edit`
- `gneiss.editor.asset-edit`
- `gneiss.editor.scene-loader`
- `gneiss.editor.save`
- `gneiss.editor.command-history`
- `gneiss.editor.author-file-save`
- `gneiss.editor.prefab-authoring`
- `gneiss.editor.runtime-scene-mirror`
- `gneiss.editor.runtime-stop-failure`
- `gneiss.editor.runtime-process`
- `gneiss.editor.runtime-scene-loading`
- `gneiss.native-scene-lifecycle`
- `gneiss.native-application-flow`
- `gneiss.native-application`
- `gneiss.scene-load-application`
- `gneiss.scene-load-preparation`
- `gneiss.prefab-preparation`
- `gneiss.render_asset_loader`
- `gneiss.texture-load-application`
- `gneiss.scene_structural_reload`
- `gneiss.prefab_structural_reload`
- `gneiss.editor.asset-scene-commands`
- `gneiss.internal-header-visibility`
- `gneiss.editor.document-editing`
- `gneiss.editor.document-actions`
- `gneiss.editor.component-edit`
- `gneiss.editor.component-lifecycle`
- `gneiss.editor.prefab-component`
- `gneiss.editor.prefab-author-operations`
- `gneiss.editor.scene-document-edit`

## 保留限制

未运行 GPU 像素验证、大场景、性能基准、浏览器、其他编译器/平台及远端 CI，不代表这些矩阵已通过。
内容冲突延迟到资源准备完成、提交前报告；没有场景修订号，不检测已恢复原值的中间修改。
本次仅本地提交，未推送或发布。

关联：[PLAN-097](../plans/PLAN-097-editor-resource-preparation.md)、
[PLAN-098](../plans/PLAN-098-runtime-reload-request-ownership.md)、
[PLAN-099](../plans/PLAN-099-document-conflict-validation.md)。
