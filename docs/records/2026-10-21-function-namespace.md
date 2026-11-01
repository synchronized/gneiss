<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 功能层领域命名空间统一

日期：2026-10-21。依据 ADR-069 与模块布局计划。

## 交付

world_internal、scene_internal、render_internal、input_internal 分别迁入
gneiss::engine::world/scene/render/input。原 engine::world 类改为
gneiss::engine::world::world，避免与领域命名空间冲突。
Engine、Runtime、Editor、工具、C 适配与测试引用同步更新，不保留旧别名。
边界检查拒绝恢复旧领域名，资源层禁止引用 Scene 的检查同步使用新命名。

共享输入值仍位于 core/input，暂随输入领域使用 engine::input；Core 收尾时需复核其归属。
没有改变资源缓存、实例所有权、RID、取消、错误或帧提交行为。
公共 C ABI 和共享公共值类型不变；内部 C++ 消费者必须与库同源码重编译。

## 验证

- Windows Clang Debug 完整构建通过。
- 53 项定向回归全部通过，涵盖 World、Scene/Prefab、输入、渲染资源、编辑、ABI 和边界检查。
- 无线程静态 CAORS Application 构建及测试通过。
- 更新共享/静态安装包后，8 项 consumer 重链接及运行通过。
- clang-format 与 git diff --check 通过。

本地日志：build/function-namespace-*.log。未运行图形窗口交互、Linux/macOS/Emscripten 或远端 Actions。

## 剩余范围

Core、Platform、C 适配辅助等命名仍需迁移；本批不表示全部架构重构或版本完成。
