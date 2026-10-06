<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Editor UI 实现与入口分离

日期：2026-10-05。

## 实施范围

将布局、主题、ImGui 适配的六个源码文件迁入 `src/editor/`，内部头改为 `.hpp`。
字体、许可证和主题来源声明随 UI 迁移。`gneiss_editor_ui` 的 target 定义进入该目录，
仍为不安装、不导出的内部静态库，只依赖 Engine 与 ImGui。

根 CMake 在 Editor 开关内创建实现目标，再装配 `apps/editor/` 宿主。程序名称、入口、
启动配置和测试入口保持不变；字体编译路径由新的源码位置生成。没有复制实现或新增状态。

Editor 作为 Engine SDK 消费者允许使用公共接口；边界检查新增正反例，拒绝 Editor
反向包含 Apps，包括相对路径。Engine 不依赖 Editor 的既有检查继续保留。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、Runtime 与边界相关 CTest 54/54 通过，141.39 秒，包含布局、窗口交互、启动、资产及灯廊工作流。
- 六个迁移源码逐文件比对，除 include 路径外内容一致；Runtime 实际链接命令不含 Editor 库。
- UI 源码 clang-format 检查通过；字体哈希与新编译路径核对通过。
- 355 个文档相对链接与提交前 git diff --check 通过。
- 本轮未重跑静态、全部 Engine 单测、跨平台和远端 Actions；公共 ABI 与安装目标未修改。

## 剩余工作

这只是 Editor 拆分的首个独立模块。会话、资产服务及主程序中的面板编排仍在 Apps，
其中会话依赖 `apps/common` 的工程与 IPC 协议，需要先核定共用能力归属再迁移。
尚未形成完整的薄入口，也没有增加编辑器插件 API。
