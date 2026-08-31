<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 日志消息契约

## 当前范围

`gneiss_log_message` 是日志生产者提交给 Engine 的 Experimental 消息描述。Application 可通过
`gneiss_application_log` 提交，并由创建描述中的可选 `log` 回调同步接收 Engine 补全后的
`gneiss_log_event`。Game Context、队列、基础 Sink 与 Editor Console 仍按
[0.13.0 计划](../plans/VER-013-0.13.0-structured-logging-console.md) 实施。

## 字段与所有权

- `severity` 必须是 `TRACE` 到 `FATAL` 之一。
- `category` 是非空 UTF-8 筛选键；建议使用短小稳定的 ASCII 名称，例如 `game` 或 `asset`。
- `message` 是 UTF-8 正文，可以为空。
- `result` 可携带相关 Gneiss 结果码；没有相关操作时使用 `GNEISS_SUCCESS`。
- `flags` 和 `reserved` 当前必须为零。
- 字符串使用指针与长度，不要求 NUL 结尾。校验和未来提交接口均不取得调用方内存所有权。

时间戳、线程标识、进程与可信来源不由生产者填写。Engine 接收消息后生成这些元数据，Game Module
也不能借此伪造 Engine 或 Runtime 来源。

## 提交与接收

- `gneiss_application_log` 可从任意线程调用，并在返回前完成字符串复制和接收回调。
- 同一 Application 的回调串行执行；事件序号按回调顺序从一开始递增。
- 回调内的事件和字符串均为借用值，只在该次回调期间有效。
- 回调不得重入日志提交；重入返回 `GNEISS_ERROR_INVALID_STATE`。
- 没有设置回调时，有效消息返回成功但不会写入文件或标准流。
- Application 销毁不能与日志提交并发；宿主应先停止生产者，再销毁 Application。

当前 Application 入口的可信来源固定为 `application`。Game Module 后续通过 Game Context 入口获得
独立来源，不能直接指定该字段。

Application 已识别的运行时诊断会在调用原有诊断回调后转换为一个日志事件。诊断模块成为事件来源，
诊断分类映射为日志分类，结果码保持不变。Application 状态创建前的配置或初始化失败没有有效会话，
因此仍只调用诊断回调。

## 校验

`gneiss_log_message_validate` 检查结构版本、严重级别、字符串边界、UTF-8 编码和保留字段。该函数
线程安全，不保存传入结构或字符串。它只验证消息描述，不提交、排队或写出日志。
