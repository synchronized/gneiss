<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS VFS 异步打开接入验证

2026-10-20，延续 [async_file 验证](2026-10-20-caors-async-file.md)。

## 实际交付

验证目标新增本地 file_system 叶后端，实现 CAORS start_open，内部复用 Gneiss uv_file_io。
JSON Loader 使用 provider 快照解析出的 resolved_file 调用 request_open，不再自行取路径打开文件。
范围读取与显式关闭沿用上一阶段适配，后端统一推进打开回执和文件回执。

链路为 asset_manager → provider 快照 → request_open → file_system::start_open → libuv，
随后 read_all_bounded → async_file → libuv，最后由 Gneiss worker 解析并交付 CAORS 缓存。
后端没有新建挂载表、资源缓存或线程池。

同步 read_all/exists 仅转发 CAORS 空根 native 后端；写入返回 unsupported。
加载链路不调用同步读取，测试使用同步结果核对异步返回字节。

## 验证

本地 Windows / Clang / Debug，警告视为错误；相关 8 项测试通过，接入测试重复 20 次通过。

- 打开期间卸载、重挂载相同 URI，旧快照仍指向旧路径，新请求使用新路径及来源标记。
- 销毁可变路由器后，旧请求继续完成；新旧读取结果与各自实际文件一致。
- 返回文件保留后端寿命，最后一个文件释放后后端销毁。
- 打开取消、无法打开路径、停止后拒绝新打开，以及已有缓存/重载/关闭回归通过。

构建及运行命令见[首轮验证记录](2026-10-20-caors-resource-probe.md)。

## 限制与后续

这仍是默认关闭的独立验证目标，Editor 与生产资产路径未切换。尚未完成 Linux、动态库、
完成队列拒绝注入、分配失败注入和大资源性能验证；不据此宣布 C51-07 D—F 全部完成。
后端借用 io/loop，宿主必须在停止推进前 abandon，排空完成队列后再停止 loop。
pump/abandon 由宿主线程串行、非重入调用。路径使用空根 native 语义，不提供沙箱隔离。

下一步抽取私有适配模块，选择正式 CPU 资源迁移，保持来源修订校验与失败回滚行为。
