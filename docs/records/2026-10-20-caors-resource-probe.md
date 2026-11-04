<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 单资源加载接入验证

2026-10-20，本地 Windows / Clang / Debug。边界见
[ADR-068](../decisions/ADR-068-caors-resource-probe.md)。

## 实际交付

新增默认关闭的 `gneiss_caors_asset_probe`，以 Temple 的真实输入映射 JSON 为输入：

1. CAORS asset_manager 根据 GUID 捕获来源快照，合并并发加载请求。
2. Loader 使用既有 libuv 异步打开、读取；宿主轮询回执，不在 worker 等待 I/O。
3. Gneiss task_executor 的独立 scope 执行 JSON 解析。
4. CAORS completion 队列在宿主线程提交缓存，返回资源 handle。

CAORS 固定于 `cbe41b75223f52bec746cc23346b28eba90fa693`（0.4.0 发布后的文档提交），
本地验证使用相同提交的源码覆盖路径。该目标静态链接 CAORS，不改变 Engine DLL 或安装 SDK。

## 复现

在仓库根目录执行，首次配置会获取固定 CAORS 源码：

```powershell
cmake --preset windows-clang-debug -DGNEISS_BUILD_CAORS_PROBE=ON
cmake --build --preset windows-clang-debug --target gneiss_caors_asset_probe -j 4
ctest --preset windows-clang-debug -R '^gneiss.caors.asset-probe$' --output-on-failure
```

已有本地 CAORS checkout 时，可增加
`-DFETCHCONTENT_SOURCE_DIR_GNEISS_CAORS=<绝对源码路径>`，需自行确认提交一致。

## 验证结果

- 编译通过（警告视为错误）；新验证连续运行 20 次通过。
- 真实 JSON 解析、同 GUID 请求共享、单订阅取消不影响另一个订阅、缓存命中通过。
- 文件不可读、可读但非法 JSON、重载失败与取消保留旧缓存通过。
- 在途请求关闭收尾、关闭后 executor 拒绝投递、libuv loop 排空通过。
- 既有 libuv 与 task-scheduler 共 7 项相关测试通过。

## 未覆盖与下一步

尚未验证 Linux、跨 DLL 部署、大资产性能、内存耗尽注入或完整编辑器链路。
JSON 只解析为文档，没有生成运行时 input-map；1 MiB 为验证限额。
当前 Loader 仅支持解析到本地绝对路径的 native 来源，不是通用 VFS 后端。

下一阶段把该桥接实现为 CAORS async_file 后端，覆盖范围读取、显式关闭和取消契约，
再接入一个正式资源类型。完整生产迁移仍按
[C51-07](../plans/UPSTREAM-051-caors-async-vfs.md)执行。
