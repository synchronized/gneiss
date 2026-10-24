<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：内部结果类型统一

## 目标与范围

用户确认 src 内部操作结果统一使用 engine::core::result，参考 Granit 的薄结果对象。
C API、外部回调和线协议边界保留明确转换，公共 C ABI 不变。
非目标：新增错误码语义、动态错误消息、异常传播、result<T> 或兼容隐式转换。
决策见[ADR-074](../decisions/ADR-074-native-result.md)。

## 实施顺序

1. 定义内部类型、静态文本和 ABI 转换；迁移核心日志、诊断值及其边界消费者。
2. 迁移核心 RID/反射、资源准备和资产请求的状态与返回值，删除内部过渡转换。
3. 迁移 World、Scene、Render、Runtime 的接口、异步完成值与诊断字段。
4. 迁移 Editor、宿主和工具层；保留必要的公共值/回调/IPC 边界适配。
5. 扫描普通内部实现中的 gneiss_result、GNEISS_ERROR_* 和公共 gneiss::result，
   排除 ABI 定义及适配边界后应无残留。

## 验证

按用户要求当前仅编译和必要源码检查，不运行行为测试、大场景或 CI。
编译期覆盖默认失败、值类型布局、禁止隐式转换、全部已知码对应和未知码往返。
静态文本、C/C++ consumer、回调及异步失败传播的行为验证延后统一执行。

## 当前状态

第一批已实现：内部结果对象、ABI 转换、核心静态文本、日志和诊断值，
以及日志提交经 Application、游戏上下文至 C API 的完整返回链。
Windows Clang Debug 开发构建通过；编译期契约、格式、源码分层及差异检查通过。
未运行测试、大场景或 CI。公共 C/C++ 结果行为仍待统一验证。

尚未完成：RID/反射、资源、World/Scene/Render、其余 Runtime、Editor 和工具层。
Application::report 及 Granit 错误映射的整数结果暂时显式构造内部值，
对应接口迁移时删除这两处过渡适配。普通内部实现最终不得依赖 engine/api 转换头。
后续批次继续复用当前分支，不把基础类型落地等同于 src 全量迁移完成。
