<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-287：VFS 读取来源基础

## 交付范围

2026-09-30 完成内部 VFS 原生读取来源，不改变公开 C/C++ SDK 或纹理磁盘格式。
设计依据为 [ADR-052](../decisions/ADR-052-texture-residency-and-budgets.md) 的读取来源/容器分层。

- `file_system::open_read` 默认返回 UNSUPPORTED，不通过整文件读取模拟区间能力。
- `virtual_file_system::open_read` 沿用 URI 校验与最长挂载点选择，失败清空来源输出。
- `native_file_system` 打开原生文件并从打开对象取得长度；来源以独立所有权持有流。
- `read_source::read_at` 读取调用者指定的范围和缓冲，检查越界及整数范围；空读取可位于 EOF，
  短读返回 IO。同来源并发读取串行保护游标，VFS 和后端销毁不影响已打开来源。
- 来源不保证内容不可变，不能代替摘要、版本身份或最终一致性校验。

## 验证

Windows Clang Debug 构建启用警告作为错误。初轮配置缺少 Threads 导入目标，随后补齐；
独立测试编译缺少库实现导出定义，已按现有测试约定修正，未关闭任何警告。

```powershell
cmake --build --preset windows-clang-debug --target gneiss_read_source_test gneiss_resource_service_test gneiss_source_revision_test
ctest --test-dir build/windows-clang-debug -R 'gneiss\.(read_source|resource_service|source-revision)$' --output-on-failure
```

最终相关回归 3/3 通过。新测试覆盖未初始化、缺失文件、空文件、非法 URI、挂载优先级、
旧后端不支持、失败清空、旧整文件接口、VFS 销毁后的生命周期、越界和最大偏移、
并发读取、外部截断的短读及错误后再次读取。

## 未完成范围

包后端、版本追踪包装、纹理头/Manifest 读取器、设备变体选择及加载服务尚未接入新入口。
现有 Runtime 仍走整文件纹理读取，不能宣称内存峰值已经降低。
本轮仅本地 Windows 验证，未运行 Linux、发布矩阵或 GPU 场景验收。
