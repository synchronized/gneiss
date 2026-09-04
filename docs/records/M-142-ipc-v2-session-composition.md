<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-142：IPC v2 会话组合实施记录

## 结果

Editor 与 Runtime 已统一使用 v2 信封、标准域集合和 Dispatcher 通讯。Transport 不再支持协议模式
切换；未发布的 v1 帧、全局消息类型、字符串能力常量、编解码实现及重复测试均已删除。

双方分别为 Session、Control、Log、Inspection、Statistics 和 Property 注册独立 Handler；类型化
Router 负责组合域注册、Dispatcher 和回调桥接，调用方只接收路由结果与解码后的强类型消息，不再
直接维护无类型上下文。Dispatcher 完成域路由后直接进入对应域解码器，不再通过集中式 `domain`
分支二次分发。

Runtime 命令与 Editor 事件使用 `std::variant` 表达互斥载荷。Runtime 状态机通过 `std::visit` 执行
命令，Editor 通过 `std::get_if` 消费事件，不再维护“种类枚举 + 所有消息字段”的无效组合。

Session 超时跟踪器归入 Session 域；Runtime 对象标识与检查顺序跟踪器归入 Inspection 域。数据域
JSON 编解码直接读写字节载荷，不再构造旧帧。Property 的 `command_id` 已从 JSON 中移除，请求与
响应只使用信封 `request_id` 关联，解码后由域适配器回填到内部命令对象。

## 验证

- Windows Clang Shared Debug：Envelope、Session、Data、Property、Transport、Runtime Session 和
  Editor Runtime Process 共 7 项关键测试通过。
- Windows Clang Static Debug：Envelope、Session、Data、Transport、Runtime Session 和 Editor
  Runtime Process 共 6 项关键测试通过。
- 类型化 Router 与变体消息重构后，Windows Clang Shared/Static Debug 下 Runtime IPC Command、
  Runtime IPC Session、Editor Runtime IPC Event 和 Editor Runtime Process 共 4 项测试均通过。
- 删除旧源文件并重新运行 CMake 配置，确认构建图不存在旧协议隐式依赖。
- 源码扫描确认生产代码和测试中不再引用 v1 帧、全局消息类型或字符串能力常量。

Static 构建期间 libuv 第三方源码仍产生既有的 Clang 警告；这些警告不来自 Gneiss 自有目标，且未
影响本次构建与测试结果。
