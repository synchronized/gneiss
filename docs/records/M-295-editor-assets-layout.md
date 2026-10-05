<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Editor 资产服务目录迁移

日期：2026-10-05。

## 实施范围

将资产浏览模型、文件监听、导入控制、重导入队列、后台工作器、创作资产监视与服务的
14 个源码文件迁至 `src/editor/`，内部头改为 `.hpp`。同步宿主、测试及基准程序的包含路径。
`gneiss_editor_assets` 的定义进入 Editor 实现目录，仍按离线导入 SDK 是否存在条件创建。
编辑会话对导入 SDK 的原有私有依赖留在原目标，不随迁移丢失。

依赖维持 Engine、Tooling、任务调度、平台 IO 与 libuv；没有添加 Apps、会话或 UI 依赖。
保持后台任务作用域、取消与关闭、监听、补扫、导入和重导入行为，不增加新的服务状态或线程。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor 与边界相关 CTest 39/39 通过，45.43 秒；覆盖后台工作器、监听、重导入、资产工作流、窗口交互及灯廊工作流。
- 14 个源码逐一比对，除 include 路径外内容一致，clang-format 检查通过。
- 七个实现文件的实际编译命令不包含仓库 Apps 路径；公共头与 ABI 无修改。
- 修改文档的相对链接和提交前 git diff --check 通过。
- 本轮未重跑静态、完整 Engine 测试或跨平台 Actions。

## 剩余工作

编辑会话、工程与 IPC 共用能力、主程序面板编排仍需拆分；测试源码暂留 Apps 下。
本轮不改变公共接口或安装目标，不代表 Editor 已成为完整薄入口或 0.45 已发布。
