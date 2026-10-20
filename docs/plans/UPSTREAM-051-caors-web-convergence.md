<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 无线程/Web 兼容需求

## 背景与复现

Gneiss 希望移除按输入、材质、Mesh 划分的临时开关，以 CAORS 统一资源基础实现。
当前锁定提交为 cbe41b75223f52bec746cc23346b28eba90fa693。
本地 Emscripten 5.0.6，未启用 pthread，执行：

```powershell
$env:EMSDK_PYTHON = (Get-Command python).Source
emcmake cmake -S build/caors-source-cbe41b7 -B build/caors-web-convergence -G Ninja `
  -DCAORS_LIBRARY_TYPE=STATIC -DCAORS_BUILD_TESTING=OFF `
  -DCAORS_BUILD_EXAMPLES=OFF -DCAORS_BUILD_BENCHMARKS=OFF
cmake --build build/caors-web-convergence -j4
```

配置成功，编译失败。本地日志为 build/caors-web-convergence-config.log、
build/caors-web-convergence-build.log。上述源码目录是本地版本覆盖，复现时也可换成同提交的
独立源码目录；构建目录须独立，不在原生构建目录里切换工具链。

首批报错：

- include/caors/meta/registry.h:179，atomic<shared_ptr<registry_snapshot>>。
- include/caors/io/file_system_resolver.h:91，atomic<shared_ptr<const route_table>>。
- include/caors/io/mount_file_system.h:83，同类路由快照。

libc++ 报告 `std::atomic<T> requires that 'T' be a trivially copyable type`。
后续还有 bind_file_system、allocate_shared 的编译错误；不能在解决首批错误前将其都认定为
独立问题。guid.cpp 另有 64 位常量到 size_t 的截断警告，建议核对 wasm32 哈希语义。
当前只证明该锁定提交和本机工具链组合失败，不推断所有 Emscripten 版本均不支持。

## 建议上游范围

1. 为共享快照读写提供能在目标标准库编译的统一内部实现；可评估互斥保护的 shared_ptr，
   保持发布/读取线程安全、不可变快照与旧引用保活，不直接换成裸指针。
2. 验证没有后台线程时，可用宿主提供的 executor 推进加载、完成回执、取消和关闭，
   不自动创建线程池或要求 libuv。
3. 提供 Emscripten 单线程静态构建与最小资源请求运行测试；原生共享库回归保持通过。
4. 不要求上游理解 Gneiss URI、场景、RID 或具体资产格式。

## 验收与 Gneiss 接入

先验收 CAORS 静态库可编译，再在单线程宿主中验证请求完成、取消后无迟到应用、
释放 manager 后外部资源句柄仍按契约有效。仅编译成功不等于异步协议可用。
原生平台补快照并发读写与旧引用保活回归。

上游兼容后，Gneiss 更新锁定版本，分离 libuv 适配装配与 CAORS 核心依赖，
用同一加载协议分别运行线程池和协作执行器，再删除临时资源开关。
本轮未修改 CAORS 源码、创建 PR 或推送上游。
