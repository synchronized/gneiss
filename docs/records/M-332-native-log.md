<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：原生日志模块首批闭环

2026-10-16，按 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 调整后的底层优先顺序实施。

## 已完成

- 日志核心使用原生 `level`、`message_view`、`event_view` 和已有强类型 `gneiss::result`。
  不再包含日志 C 头或使用 `gneiss_log_*`、`GNEISS_LOG_*`；结果值仍复用基础 C/C++ 错误码契约。
- 原生提交校验级别及 UTF-8 文本；ABI 布局校验与级别数值映射留在接口层。
- Application 和 Render 日志生产者、Runtime 消费边界已接入；沿用同一队列及线程。
  Runtime 现有日志/IPC 消费协议仍使用 C 值结构，转换留在宿主边界，不反向进入核心。
- 增加日志核心禁止依赖 ABI/上层模块的检查及反例，保留公共 C/SDK 接口和数值布局。

## 验证

本地 Windows Clang Debug 构建 Engine、Editor、Runtime 与相关测试成功。
17 项日志、Application、IPC、宿主冒烟及边界测试通过。
原生测试覆盖无效级别/文本、4 个生产线程共 256 条消息、队列溢出、提交后原文本修改、
序号、回调重入拒绝、接收方异常及析构排空。静态检查覆盖日志投递器及其测试。

未执行 Linux、静态安装矩阵和完整 Sponza；未宣称 Application 或 0.51 整体完成。
后续继续 Input，再收敛 Render 服务接口；基础结果头彻底脱离 C ABI 不是本轮已完成项。
