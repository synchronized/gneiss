<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：公共对象 SDK 删除验收

日期：2026-10-21。实施依据：[ADR-070](../decisions/ADR-070-remove-public-object-sdk.md)。
这是 0.51 开发分支的局部架构调整，不代表版本完成或发布。

## 交付

删除 gneiss::sdk 对象包装、对象入口与私有 inl，保留 C ABI、共享值类型和无状态辅助。
安装文件集、示例、游戏模块模板、C++ consumer、编辑器及场景行为测试同步迁移。
删除仅服务于 SDK 所有者、移动/接管、回调包装的测试；实际资源、场景、属性及编辑行为
继续由原有 C ABI/原生测试与迁移后的测试覆盖。测试清理守卫与示例 scope_exit 不安装。

旧的 C 函数到对象包装清单不再适用，替换为公共定义完整性和禁止 SDK 回流检查；
C 类型与常量清单仍完整校验，必须提供值类型映射或 ABI 专用理由，不接受 pending。
负向夹具覆盖缺失/重复定义、SDK 回流、已删除入口回流及未登记 C 类型。

## 本地验证

- Windows Clang Debug 完整构建通过，包括独立 C11 头与剩余 C++20 值头。
- 61 项相关无窗口测试：首轮 60 项通过，负向夹具的标量匹配写法修正后单项复验通过。
  后续启用严格值类型审核后，公共接口正向与负向检查均通过。
- 保留并迁移绘制值描述/借用数组的提交测试，通过单项验证；游戏上下文成功动作查询、
  ABI 清单及 0.9/1.0 C ABI consumer 共 4 项复验通过。
- 无线程静态构建的 CAORS Application 目标构建及测试通过。
- 全新共享与静态安装前缀：分别构建并通过 4 项 consumer，共 8 项，包含 C、C++、
  Application/World 与属性检查示例；配置阶段验证已删除头无法包含。
  共享 consumer 使用现有匹配的 Granit 0.67 包配置解析依赖。
- 游戏模块模板、可选 profiling_capture 源码通过 C++20 语法检查。

构建与测试原始日志位于本地 build/sdk-removal-*.log，不作为仓库交付物。

## 未验证与限制

本次未重新运行图形窗口、Sponza、Tracy 实际采集、Linux/macOS/Emscripten 或远端 Actions。
公共 C++ 对象调用方需要源码迁移；本次没有为其提供兼容层，也未声称完成整体架构调整。
