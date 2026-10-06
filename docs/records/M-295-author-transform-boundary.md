<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Runtime Transform 回写脱离 IPC

日期：2026-10-05。

## 实施

将 Runtime 变换回写从 Apps 迁至 `src/editor/runtime_author_apply.hpp/.cpp`，归独立会话目标。
输入改为三个 UUID 视图与局部变换；Apps 从协议节点提取这些值，不向 Editor 传 IPC 对象。
命令复制身份字符串后记录 Undo/Redo，不保留输入视图。补充分配失败及其他异常的结果码转换，
避免 noexcept 因复制命令身份或构造回调分配失败而终止进程。

会话综合测试现在只链接独立会话库。新增临时输入字符串销毁后撤销重做的验证；
原有普通节点、Prefab 身份、无效映射和失效映射测试保留。
边界检查新增拒绝 `gneiss/app/` 与宿主 `ipc_*_protocol.h` 的正反例，避免短包含路径绕过目录约束。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、工程/IPC 与边界 CTest 49/49 通过，47.13 秒。
- 会话测试的实际链接命令不含宿主、工程和 IPC 协议库。
- 回写实现、头与修改测试的 clang-format、文档相对链接及 git diff --check 通过。
- 未进行分配失败故障注入，未重跑静态、完整 Engine 单测、跨平台或远端 Actions。

## 剩余工作

场景镜像、属性响应、控制台仍使用协议类型，工程与进程装配、面板编排仍待拆分。
公共 SDK 与 IPC 格式未变。本轮不代表整个运行时同步或 0.45 发布已经完成。
