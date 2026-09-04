<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-142：IPC v2 会话组合实施记录

## 结果

Editor 与 Runtime 已统一使用 v2 信封、标准域集合和 Dispatcher 通讯。Transport 不再支持协议模式
切换；未发布的 v1 帧、全局消息类型、字符串能力常量、编解码实现及重复测试均已删除。

Session 超时跟踪器归入 Session 域；Runtime 对象标识与检查顺序跟踪器归入 Inspection 域。数据域
JSON 编解码直接读写字节载荷，不再构造旧帧。Property 的 `command_id` 已从 JSON 中移除，请求与
响应只使用信封 `request_id` 关联，解码后由域适配器回填到内部命令对象。

## 验证

- Windows Clang Shared Debug：Envelope、Session、Data、Property、Transport、Runtime Session 和
  Editor Runtime Process 共 7 项关键测试通过。
- Windows Clang Static Debug：Envelope、Session、Data、Transport、Runtime Session 和 Editor
  Runtime Process 共 6 项关键测试通过。
- 删除旧源文件并重新运行 CMake 配置，确认构建图不存在旧协议隐式依赖。
- 源码扫描确认生产代码和测试中不再引用 v1 帧、全局消息类型或字符串能力常量。

Static 构建期间 libuv 第三方源码仍产生既有的 Clang 警告；这些警告不来自 Gneiss 自有目标，且未
影响本次构建与测试结果。
