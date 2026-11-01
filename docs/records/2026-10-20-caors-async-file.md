<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS async_file 与 libuv 范围读取、关闭验证

2026-10-20，延续[单资源加载验证](2026-10-20-caors-resource-probe.md)。

## 交付

- libuv 文件新增 begin_close / poll_close。拒绝新读，保留在途读，实际关闭完成后才返回终态。
- 重复关闭共享结果；析构仍为非等待式关闭兜底。同步读取也计入在途数，关闭不持锁等待磁盘。
- 验证程序新增 CAORS async_file 适配，支持精确范围、预算、取消及显式关闭。
- JSON Loader 的读取改走 read_all_bounded → async_file → libuv；解析仍在 Gneiss worker 执行。
- 宿主 abandon 清理完成凭证，避免请求状态与文件互相保活；底层真实 I/O 最终由 loop 排空。

## 验证与边界

Windows Clang Debug，警告视为错误。相关 libuv、调度器、单资源共 8 项测试通过。
异步文件与 CAORS 验证各重复 20 次通过。新增检查覆盖：

- 在途读期间关闭、新读拒绝、重复关闭及关闭结果查询。
- 两个不同 offset 的范围读取、越界和预算拒绝、请求取消。
- 放弃请求后取消终态与适配对象释放，已有共享加载、缓存和失败重载继续通过。

编译与执行命令沿用上一份记录；底层测试目标为 gneiss_uv_async_file_test。

本次仍未切换 Editor，也未实现 CAORS file_system::start_open；来源解析后打开仍由原有 libuv 回执执行。
适配保留在验证目标内，主线程 pump 会把 libuv 缓冲复制到 CAORS 缓冲，当前限额为 1 MiB，
不代表大资产吞吐或峰值内存已经验收。尚未验证 Linux、关闭失败注入和内存分配失败注入。

下一步完成 VFS 异步打开和来源寿命测试，再抽取私有模块、迁移正式 CPU 资源。
