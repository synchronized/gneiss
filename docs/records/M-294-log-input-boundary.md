<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-294：内部日志输入与回调边界核对

日期：2026-10-06。

## 变更

Application 与 Game Context 的日志入口改为内部 `message_view`，公共布局、指针/长度和 UTF-8 校验
留在 API。诊断转日志直接提交文本视图，不再生成中间 C 消息。Application 身份和模块来源仍由
内部解析并覆盖，不允许外部文本视图伪造来源；原有串行投递、文本复制和关闭排空语义不变。

当前 Core/Function 中保留的结构尺寸处理已逐项核对：Reflection 冻结元数据的 C 借用视图和属性
回调值用于真实公开协议；Application 的诊断和日志事件用于真实 C 回调。它们不是内部业务描述，
无需建立第二份元数据注册表或复制整套属性值运行时。依据见 [ADR-053](../decisions/ADR-053-cpp-core-and-c-abi.md)。

## 验证

- 共享完整构建通过；C 日志、Application、Game Context、投递器、Runtime 日志、接口清单和边界相关测试 10/10 通过，10.62 秒。
- 静态专项构建通过；C 日志、Application、Game Context、接口清单与边界正反例 8/8 通过，10.68 秒。
- 新增 nullptr/零长度空文本经 Application 提交、回调及关闭排空的验证；原有跨线程临时字符串、模块来源、序列和重入用例保留。
- clang-tidy 检查日志及 Game Module 入口，保留既有相邻整数句柄提示，未降低检查等级。

Application/World/Scene 内部参数迁移已完成本地验证；最终共享/静态整合、平台矩阵和编辑器余项仍属于版本验收范围。
