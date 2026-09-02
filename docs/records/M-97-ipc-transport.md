<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-97 IPC Transport 实施记录

## 结论

M-97 已完成仅限本机回环的单连接 IPC Transport。Transport 复用 `uv_runtime` 的专用 I/O 线程，
提供 Server、Client、版本化帧收发、连接事件、有界事件队列和有界发送队列，不理解 Editor 的
Play、Pause 等业务语义，也不形成公共 Network Service。

## 当前行为

- Server 只监听 `127.0.0.1` 的系统分配端口，Client 只接受有效的 IPv4 回环端点。
- libuv loop、TCP handle、连接、读取、写入与关闭操作全部归 I/O 线程所有。
- 后端通过内部 `uv_runtime_access` 获得 loop；libuv 类型未进入普通接口或 Engine 公共 API。
- 每条连接使用 16 字节版本化帧头和有界增量解码器，支持半包与粘包。
- 事件与待发送写入均有固定容量；容量耗尽时返回 `not_ready` 或记录丢弃数量。
- 控制事件优先于已排队的数据帧事件，避免数据洪水完全遮蔽连接和错误状态。
- Server 在客户端断开后回到监听状态；完整 Stop 后，同一 Transport 对象可以重新启动。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| Server/Client 建连与状态事件 | 通过 |
| Client 到 Server、Server 到 Client 帧传输 | 通过 |
| 帧字段和 payload 完整性 | 通过 |
| 客户端断开、Server 恢复监听与新 Client 重连 | 通过 |
| Server/Client 对象 Stop 后重新启动 | 通过 |
| 无效端点与连接拒绝 | 通过 |
| 有界事件队列及控制事件优先 | 通过 |
| Windows Clang 专项压力重复 | 连续 100 次通过 |
| Windows MSVC 专项构建与测试 | 通过 |
| Windows Clang Debug 完整回归 | 98/98 通过 |

Linux、Sanitizer 与最终 Shared/Static 验证仍由 M-101 统一执行。M-98 将在 Transport 上增加握手、
版本协商和 Editor–Runtime 业务消息，不改变 Transport 的字节与连接职责。
