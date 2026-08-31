<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 日志消息契约

## 当前范围

`gneiss_log_message` 是日志生产者提交给 Engine 的 Experimental 消息描述。当前版本已经提供 C11
结构、C++20 构造辅助和独立校验；Application、Game Context、队列、Sink 与 Editor Console 的提交
链路仍按 [0.13.0 计划](../plans/VER-013-0.13.0-structured-logging-console.md) 实施，不能把校验接口视为
日志已经写出。

## 字段与所有权

- `severity` 必须是 `TRACE` 到 `FATAL` 之一。
- `category` 是非空 UTF-8 筛选键；建议使用短小稳定的 ASCII 名称，例如 `game` 或 `asset`。
- `message` 是 UTF-8 正文，可以为空。
- `result` 可携带相关 Gneiss 结果码；没有相关操作时使用 `GNEISS_SUCCESS`。
- `flags` 和 `reserved` 当前必须为零。
- 字符串使用指针与长度，不要求 NUL 结尾。校验和未来提交接口均不取得调用方内存所有权。

时间戳、线程标识、进程与可信来源不由生产者填写。Engine 接收消息后生成这些元数据，Game Module
也不能借此伪造 Engine 或 Runtime 来源。

## 校验

`gneiss_log_message_validate` 检查结构版本、严重级别、字符串边界、UTF-8 编码和保留字段。该函数
线程安全，不保存传入结构或字符串。它只验证消息描述，不提交、排队或写出日志。
