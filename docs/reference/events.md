<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 同步对象通知

内部头 `engine/core/events/signal.hpp`，命名空间 `gneiss::events`。
使用者链接内部 CMake 目标 `gneiss_events`；不安装 SDK，不引入 Engine 或 GPU 运行时。

```cpp
events::signal<void(const change&)> changed;
events::scoped_connection subscription = changed.sink().connect<&panel::refresh>(panel_instance);
changed.publish(change_value);
```

Signal 由发布者独占且不可复制、移动。Sink 只提供订阅权限，不保活 Signal。
`connect<&function>()` 绑定自由函数，`connect<&type::method>(object)` 借用监听对象。
同一目标重复订阅会独立交付，断开其中一次不影响其他订阅。不提供拥有型捕获 lambda 接口。

`connection` 复制共享订阅身份，析构不自动断开；`disconnect()` 可重复调用。
`scoped_connection` 只能移动，析构自动断开，移动赋值先断开原连接。
`connected()` 只表示订阅仍有效，不检测监听对象是否存活。
Signal 先销毁后，连接查询返回 false、断开为空操作；过期 Sink 不能建立新连接。

所有操作限创建 Signal 的线程。参数仅在同步调用期间有效，不保证监听顺序。
禁止同一 Signal 递归发布，以及发布期间连接、断开、销毁 Signal 或监听对象。
普通操作的契约违例抛出 `std::logic_error`，不转换为业务 result；析构时违例会终止进程。
监听对象必须在销毁前断开其订阅。订阅不保活监听对象，也不自动检测悬空对象。
回调异常原样传播并中止本轮发布；已经执行的监听者不回滚，发布状态会恢复。

该接口尚未替换生产通知。后续接入和独立日志工作见 [PLAN-116](../plans/PLAN-116-events-and-logging.md)。
