<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-186：重复实现清理与版本验收

## 结果

Gneiss 已删除私有 Shader 清单解析、自有同构 PBR Material 归档和 Granit 源码目录推导，统一依赖
Granit 0.12 的公开 Shader Asset、标准 PBR 与 RenderPipeline 资产目录契约。版本号已提升至
0.28.0，路线图和变更记录同步收口。

本次未发现需要继续提交给 Granit 的通用缺口；Gneiss 作者资产、RID、Scene 与 IPC 边界保持不变。

## 本地验证

- Windows Clang Shared Debug：完整构建及 132/132 测试通过。
- Windows Clang Static Debug：完整构建及 130/130 测试通过。
- Windows MSVC Shared Debug：完整构建及 132/132 测试通过。
- 安装消费、C11/C++20 头文件、旧 ABI consumer、Granit Runtime、Lantern Gallery、Vulkan SPIR-V、
  WebGPU WGSL sidecar 和标准 PBR Material 完整性均包含在上述矩阵中。
- 静态构建首次验收暴露停止协议冷启动竞态；测试现等待 Runtime 完成启动后再发送停止信号，完整
  复跑通过。

## 待远端验证

Linux Clang/GCC Shared/Static、Granit Runtime 和 Sanitizer 由手动 Actions 覆盖；需要在分支推送后
按发布工作流运行，不将尚未执行的远端矩阵记为已通过。
