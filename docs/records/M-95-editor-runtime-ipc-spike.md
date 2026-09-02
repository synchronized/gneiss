<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-95 Editor–Runtime IPC 架构与协议 Spike 记录

## 结论

M-95 已验证采用 libuv 专用 I/O 线程的基础路径可行。Gneiss 以私有静态目标接入 libuv 1.52.1，
`uv_runtime` 能在 Windows Clang 和 MSVC 下完成跨线程有界任务投递、异常隔离、安全关闭和重复启动。
下一阶段可以在该执行器上实现回环 TCP Transport；本记录不表示双向 IPC 或 Network Service 已完成。

## 依赖输入

| 项目 | 结论 |
| --- | --- |
| 上游 | [libuv 官方仓库](https://github.com/libuv/libuv) |
| 版本 | 1.52.1 Stable |
| 获取 | [官方发布归档](https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz) |
| SHA-256 | `66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7` |
| 许可证 | MIT；安装时复制为 `libuv-LICENSE` |
| 链接 | 私有静态 `uv_a`，不进入 Gneiss 公共 API |

配置前检查未在 Gneiss 或当前 Granit 构建树中发现 libuv 目标或链接输入。首次 Git 浅克隆在本地 HTTPS
阶段无进展，因此改用带摘要校验的官方发布归档，避免配置依赖 Git 传输状态。

## 已验证行为

- `uv_loop_t` 和 `uv_async_t` 只在专用 I/O 线程初始化、运行和关闭。
- 主线程通过容量明确的队列提交拥有所有权的任务；未启动、停止中和队列满均返回失败结果。
- Stop 停止接受新任务，唤醒 loop，执行已接收任务，关闭 async handle，关闭 loop 并 join 线程。
- 任务异常被限制在 libuv 的 C 回调边界内，不会退出 I/O 线程或穿过 C ABI。
- 同一 `uv_runtime` 可以在完整停止后重新启动；析构路径复用正常停止流程。
- libuv 原生类型只存在于 `.cpp` 实现，不通过内部头或普通 Consumer 传播。

## 验证结果

| 环境 | 结果 |
| --- | --- |
| Windows Clang Debug 专项构建 | 通过 |
| Windows Clang Debug 专项测试 | 通过 |
| Windows Clang 重复测试 | 连续 100 次通过 |
| Windows Clang Debug 完整回归 | 94/94 通过 |
| Windows MSVC Debug 专项构建 | 通过 |
| Windows MSVC Debug 专项测试 | 通过 |

libuv 上游 C 目标在 Windows Clang/MSVC 下会报告转换、弃用函数等自身警告。第三方目标未继承 Gneiss
自有目标的警告即错误策略；`gneiss_uv_runtime` 和测试仍使用项目严格警告配置。后续 Linux 与
Shared/Static 完整矩阵归入 M-101，不在本次 Windows Spike 中推断。

## 后续边界

- M-96 继续补足执行器的错误报告、关闭竞态和高并发压力测试。
- M-97 在 `uv_runtime` 上建立只处理字节与连接的回环 TCP Transport。
- 业务握手、令牌、状态和 Play 控制留在 M-98 及以后，不下沉到 I/O Core。
