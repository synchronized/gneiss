<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# resource 目录合并验收

2026-10-21，0.51 分支；对应[模块布局计划](../plans/PLAN-051-module-layout.md)的资源目录部分。

## 已落地布局

```text
src/engine/
  core/
  platform/
  resource/       58 个现有资源文件，统一管理器、描述、加载、缓存及 VFS
  function/       运行时 Input、Scene、World、Render、Application 等
  api/
```

- engine/asset 与 function/asset 已合并到 engine/resource，旧目录删除。
- prefab_asset_loader 迁入 resource，Prefab 实例化、覆盖编辑继续留在 function/scene。
- 更新自有源码、CMake、探针、测试、内部头清单及边界规则，不保留转发头或旧 include 路径。
- 资源模块不能反向包含功能层或宿主；新增旧资产目录重现的负向测试。
- 类型、序列化格式、公开 SDK 路径与 ABI 不变，本次没有改变加载或缓存协议。

resource 目前仍为平铺文件；目标中的 types/loaders/vfs/backend 子目录与 src/runtime
尚未落地，不把本次目录合并描述为完整布局计划完成。已有命名空间保持不变。

## 验证

- Windows Clang Debug 完整默认构建成功，覆盖 Editor、Runtime、离线工具、示例及测试程序。
  构建日志：build/resource-layout-build.log。
- 15 项资源/输入/Scene/Prefab/CAORS/边界测试通过。
- 额外内部头可见性、任务头可见性、Editor 资产场景命令 3 项测试通过，共 18 项。
- 原生无线程静态 sc-mat-s 的 Application 目标重新构建并测试通过。
- 代码与构建文件旧 include 路径扫描、git diff --check 通过。

本轮主要为路径与归属迁移，未执行完整 CTest、GUI 手工、Linux、Web 或完整安装矩阵；
未声称既有静态分析告警已消除。历史 Record 保留当时路径，当前布局以
[源码目录说明](../concepts/repository-layout.md)为准。
