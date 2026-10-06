<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-304：原生日志提交描述

2026-10-06，0.47 开发分支的日志生产者迁移；M-304 整体与版本发布尚未完成。

## 交付与边界

`log_message` 使用独立 C++ 结构、强类型级别、字符串视图及 result；Application 和 Game Context
提交入口、校验函数、工厂函数均接入该类型。C ABI 互操作通过显式 to_native，原 C 接口不变。
具体字段、默认值及字符串寿命见[日志参考](../reference/logging.md#c-日志提交)。

本组不引入回调存储、额外队列或日志缓存，不改变日志线程及关闭排空行为。
日志事件和接收回调仍使用 C 类型。Application 原生描述迁移前，必须解决 release() 转移裸句柄
时适配上下文的所有权问题；待办见 [DEV-047](../plans/DEV-047-native-cpp-sdk.md)。

清单新增确认 3 个函数、2 个类型及日志生产者常量；其他 pending 项没有批量标记完成。
日志级别保留 32 位底层表示，确保非法扩展值未经截断交给校验；enum-size 检查的局部说明与此对应。

## 验证

Windows Clang 开发环境，保留 warnings-as-errors：

- 共享配置全量构建通过；共享/静态 C++ 公共头独立编译通过。
- 共享日志、Game Context 和清单回归 7/7；静态日志与 Game Context 回归 5/5。
- 新安装前缀的共享/静态消费者分别 3/3，C++ 消费者实际提交原生日志描述。
- cpp_log_test 与 log.hpp 的 clang-tidy 通过，修改源码格式与 diff 检查通过。

覆盖默认描述的空分类拒绝、非法枚举值、非法 UTF-8、空消息和正常中文文本；C 互操作保留指针
借用、结果码及零保留字段。Application 回归覆盖临时文本提交后释放、跨线程生产、重入拒绝、
回调抛异常后的排空与 C++ operation 到接收事件结果码的传递。

未运行远端矩阵、整版静态回归或发布；Application 原生回调、其他模块及旧路径移除仍待完成。
