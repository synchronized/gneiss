<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：日志事件投递与 Application 回调边界

日期：2026-10-05。接续 Core 目录迁移，拆开旧日志投递器中的 Application 协议依赖。

## 所有权与调用方向

投递器迁至 `src/engine/core/log/`，接收 C++ `message_view` 并在返回前复制字符串，单消费线程
向 `event_sink` 投递临时 `event_view`。上下文仅为调用方解释的整数，不持有 Application。
接收函数由投递器持有；关闭时先处理队列并等待工作线程退出，再释放接收函数。

Application 在 `application_log_sink.hpp` 中将内部事件转为借用的 C 事件，复制回调函数与
user_data 配置；user_data 仍须存活至 Application 关闭完成。不捕获 Application 对象地址，
不新增注册表或运行时状态。适配回调允许异常返回到投递器的隔离层，不使用 noexcept 提前终止。

渲染后端直接提交内部消息视图。Core 继续复用公共结果码与日志级别常量，但不构造公共日志
消息、事件或使用 Application 回调类型。公共 ABI、保留字段、可信来源与错误语义保持不变。

## 验证范围

投递器独立测试不再链接 Engine，覆盖空接收函数、零容量、回调抛异常后继续消费、重入拒绝、
析构排空，以及既有背压顺序。Application 测试保留文本复制、多线程串行回调、旧句柄失效、
无接收函数行为，并新增 C 回调异常隔离与关闭排空验证。

本轮不改用统一任务池替代日志消费线程；回调内 flush 或销毁投递器仍不允许。

## 验收结果

共享与静态相关目标构建通过；日志、Core 边界、Editor/Runtime 启动与安装消费者各 12/12
通过（11.06 秒 / 16.17 秒）。共享灯笼场景与 Editor—Runtime 工作流另有 2/2 通过
（12.95 秒），覆盖实际场景加载后的日志与生命周期协作。

两种配置的投递器测试链接规则均不包含 Engine，显式链接线程依赖。投递器实现 clang-tidy
无自有代码告警；修改的独立头、实现和投递器测试格式检查通过，文档链接与差异检查通过。
初次新增测试编译暴露缺省视图初始化告警，补齐显式默认初始化后完成上述验证，没有关闭警告。

未重跑全量 CTest、远端 Actions、Linux/macOS 或整引擎 Web 验证；当前版本仍在开发。
