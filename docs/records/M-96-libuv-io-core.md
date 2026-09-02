<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-96 libuv I/O Core 实施记录

## 结论

M-96 已完成内部 libuv I/O Core。`uv_runtime` 提供专用 loop 线程、有界多生产者任务队列、任务异常
计数、显式结果码映射、排空关闭和重复启动；不暴露 libuv 类型，也不包含 TCP 或 Editor–Runtime
业务协议。

## 实施边界

- `from_uv_error()` 将常用 libuv 错误映射为稳定的 `gneiss::result`，未知 I/O 错误统一降级为
  `result::io`。
- `post()` 可以由多个线程并发调用；其余生命周期操作要求调用方外部同步。
- 队列满、未启动或停止中拒绝新任务，不进行无界增长。
- 已接受的任务在 Stop 和析构时排空；任务异常不会越过 libuv C 回调边界，并计入失败任务数量。
- I/O 线程内调用 Stop 返回 `invalid_state`，避免线程自行 join。
- 完整停止后可以复用同一个对象重新创建 loop 和 async handle。

## 关闭竞态修复

首轮并发 Stop 压力测试复现了 `uv_async_send()` 与 `uv_close()` 的竞争：生产者完成入队并释放锁后，
I/O 线程可能先关闭 async handle，生产者随后发送唤醒并触发 libuv 断言。

修复后，状态检查、任务入队和 async 唤醒位于同一临界区；Stop 的状态切换和最终唤醒也使用同一把
锁。I/O 回调必须获得该锁后才能观察停止状态并关闭 handle，因此不存在“关闭后发送”窗口。发送失败
时同步撤回刚入队的任务，保持返回结果与任务所有权一致。

## 测试结果

| 验证 | 结果 |
| --- | --- |
| 错误码映射 | 通过 |
| 未启动、重复启动、重复停止和队列容量 | 通过 |
| 任务线程归属、异常隔离与失败计数 | 通过 |
| I/O 线程误调用 Stop | 通过 |
| 8 个生产者、每个 1000 个任务 | 全部接受任务均执行 |
| 并发生产与 Stop | 接受数与执行数一致 |
| 析构排空 | 通过 |
| Windows Clang 压力重复 | 修复后连续 200 次通过 |
| Windows MSVC 三项专项重复 | 连续 25 次通过 |
| Windows Clang Debug 完整回归 | 96/96 通过 |

Linux Sanitizer 和最终 Shared/Static 矩阵在 M-101 统一执行。M-97 可以在现有任务执行器之上建立
Transport，连接与读写 handle 仍只由 I/O 线程拥有。
