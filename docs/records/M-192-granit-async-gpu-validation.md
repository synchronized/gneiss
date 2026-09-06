<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-189～M-192：Granit 0.14 异步 GPU 契约接入与版本验收

## 结果

Gneiss 已升级到 Granit 0.14.0 发布提交
`74a1083b9c8182168ed814c594f6186f0b0fa06e`，PACKAGE 最低版本同步提升至 0.14。Granit
RenderPipeline 的公开指标接口保持兼容，内部异步 Timestamp 操作由管线拥有并清理。

Gneiss 删除了待处理 Frame 序列队列，直接报告 Granit 的 `sample_sequence`。该序列表示已发布 GPU
样本，不宣称对应某个 Gneiss Frame；后端跳过采样时不会造成后续指标错配。异步 Upload Batch 属于
后续资源流送工作，本版本不启用。

版本号已提升至 0.29.0，未改变公共 C ABI、场景格式、资产格式或 IPC 协议。

## 本地验证

- Windows Clang Shared Debug：完整构建及 132/132 测试通过。
- Windows Clang Static Debug：完整构建及 130/130 测试通过。
- Windows MSVC Shared Debug：完整构建及 132/132 测试通过。
- 上述矩阵包含默认缓存迁移、安装消费、C11/C++20 头文件、旧 ABI Consumer、Runtime、Editor、
  Lantern Gallery、Granit 平台冒烟和资源归零检查。

## 远端验证

- [Linux Actions 34019949762](https://github.com/synchronized/gneiss/actions/runs/34019949762)：
  Clang/GCC Shared/Static、Granit Runtime Shared/Static 和 Sanitizer 共 7 个作业全部通过。
- [Windows Actions 34019951250](https://github.com/synchronized/gneiss/actions/runs/34019951250)：
  MSVC Runtime Shared/Static 与安装 Consumer Shared/Static 共 4 个作业全部通过。

Windows 工作流提示 `ilammy/msvc-dev-cmd@v1` 仍声明 Node.js 20；GitHub 当前强制使用 Node.js 24，
本次执行成功。该依赖升级属于后续工作流维护，不阻塞 0.29.0。
