<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 日志消息契约

## 当前范围

内部宿主使用原生 `application::submit_log`。消费线程通过 `log` 回调接收 `event_view`。
日志队列、基础 Sink、Runtime 传输协议与 Editor Console 保留；日志 C API 已撤销。

## 字段与所有权

- `severity` 必须是 `TRACE` 到 `FATAL` 之一。
- `category` 是非空 UTF-8 筛选键；建议使用短小稳定的 ASCII 名称，例如 `game` 或 `asset`。
- `message` 是 UTF-8 正文，可以为空。
- `result` 可携带相关 Gneiss 结果码；没有相关操作时使用 `GNEISS_SUCCESS`。
- 字符串使用 string_view 借用，不要求 NUL 结尾。校验和提交接口均不取得调用方内存所有权。

时间戳、线程标识和事件序号由投递器生成，来源由内部调用方填写。

## 提交与接收

- 原生 `application::submit_log` 可从任意线程调用，并在返回前完成字符串复制和入队，不等待 Sink。
- 同一 Application 的回调在专用消费线程串行执行；事件序号按入队顺序从一开始递增。
- 队列有固定上限；已满时丢弃新事件并在恢复后生成包含丢弃数量的 `backpressure` 告警。
- 回调内的事件和字符串均为借用值，只在该次回调期间有效。
- 回调不得重入日志提交；Application 仍有效时重入返回 `GNEISS_ERROR_INVALID_STATE`。
  关闭排空期间接收方只应消费事件，不再向正在关闭的父对象提交。
- 没有设置回调时，有效消息返回成功但不会写入文件或标准流。
- Application 销毁不能与日志提交并发；宿主应先停止生产者，再销毁 Application。
- 原生 `application::submit_log` 允许工作线程提交；调用方必须先停止并等待提交线程，再关闭或释放 Application。
  最后一个原生引用释放前排空已接收事件，回调捕获的数据 必须存活至关闭返回。

独立 Runtime 已将 Application 日志回调接入标准流和单文件轮转 Sink。日志文件保持人类可读文本，
标准输出则使用 `@gneiss-log-v1 ` 前缀加单行 JSON。协议字段包含版本、单调时间、序号、级别、来源、
分类、线程、结果码和消息；字符串按 JSON 规则转义。Application 创建前的参数与工程加载失败，以及
协议编码失败，仍使用原始文本降级通道。普通输出、未知协议版本和格式错误的协议行应由消费方保留为
Raw 输出，不能静默丢弃。

共享行解码器允许消费方按任意字节块输入。单行默认最多保留 64 KiB；超限行会标记为截断并丢弃
剩余字节，但不会影响下一行。子进程退出后，消费方必须调用 `finish`，以交付最后一个没有换行符的
尾部。该限制作用于跨进程传输，不改变进程内日志消息契约。

Editor Console 将协议事件与 Raw 行按到达顺序保存在最多 4096 条的有界记录中，并显示累计移除数量。
每次 Runtime 启动使用新的会话 ID。面板可以按严重级别、当前会话、来源、分类和文本组合筛选，支持
暂停可见截止位置、清空、复制当前结果及自动滚动。暂停只冻结显示，后台仍继续接收有界日志。

Application 已识别的运行时诊断会在调用原有诊断回调后转换为一个日志事件。诊断模块成为事件来源，
诊断分类映射为日志分类，结果码保持不变。Application 状态创建前的配置或初始化失败没有有效会话，
因此仍只调用诊断回调。

## 校验

原生 `engine::validate_message` 校验严重级别、字符串边界和 UTF-8 编码；模块日志入口
另外校验结构版本、指针长度和保留字段。校验不保存消息或字符串，不提交、排队或写出日志。
日志校验 C 导出已删除，原生提交入口负责消息语义校验。

## 原生 C++ 日志提交

内部只使用 `gneiss::engine::message_view` 和 `event_view`，值定义位于 `core/log/log_types.hpp`。
Runtime 文件输出、IPC 编码与 Editor 控制台不依赖 C 日志事件布局；编码时复制借用文本，
不保留事件视图。消息默认级别为 info、结果为 success；
分类必须非空，消息允许为空。字符串通过 string_view 借用到同步提交返回；投递器复制文本后入队。
原生 Application 无日志回调时仍校验消息，合法消息直接返回成功，不创建投递队列。

```cpp
const auto status = application.submit_log({
    .severity = gneiss::engine::level::warning,
    .source = "assets",
    .category = "loading",
    .message = "资源尚未就绪",
    .result = gneiss::engine::result::not_ready,
});
```

其中 application 是有效的原生 Application 对象，内部调用方填写来源。
消息级别和 UTF-8 规则集中在 `engine::validate_message`，不经过 C API。
旧 `log_message`、`log_event`、`log_severity` 包装和构造/转换辅助已删除，不安装公共 C++ 日志头。

原生 `shutdown` 关闭运行服务；日志投递器在 Application 释放时排空并等待消费线程结束。
关闭期间的内部诊断仍可入队，因此须在释放 Application 前保持接收回调捕获的对象有效。
