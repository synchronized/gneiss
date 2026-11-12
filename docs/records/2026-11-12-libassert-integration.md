<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# libassert 接入验证

在用户已有 CMake 草稿上接入 libassert 2.2.1、cpptrace 1.0.4，固定归档摘要并使用统一缓存。
Engine 私有静态链接；内部断言入口与使用约束见[代码规范](../guides/coding-style.md#运行时断言)，
依赖安装与平台差异见[第三方声明](../../THIRD_PARTY_NOTICES.md)。
首个实际消费者是 GameObject 原有的有效性断言；没有批量替换结果类型或业务校验。

构建验证暴露并修正了两个既有问题：PBR 嵌入源码列表在赋值前被使用，以及静态 Engine
导出引用未导出的 fatal 目标。生成文件现加入真实构建依赖；静态 Engine 自带 fatal 实现，
原生文件后端的构建期链接不再泄漏到安装导出。

## 本地结果

- Windows Clang Debug：共享 Engine、断言测试、最小示例与原生渲染生命周期测试构建通过。
- 5 项 CTest 通过：assert-debug、assert-release、minimal-example、native-render-lifecycle、core-boundary。
- 子进程验证 Debug/Release 求值行为、ASSERT/PANIC 失败诊断；测试 handler 使用确定退出码，
  不触发系统 abort 对话框。未把生产默认 abort 改为异常或业务日志调用。
- 单独 Windows 静态配置在 `GNEISS_DEPENDENCY_OFFLINE=ON` 下生成、构建 Engine、安装通过。
- libassert/cpptrace 安装后，独立 C++ 消费者 find_package、编译、链接和运行通过。
- 静态 Engine 安装消费者检查停在既有外部 Granit package 前置条件：验证环境没有已安装的
  Granit 0.67.0 package；不将静态 Engine 下游链接标记为已通过。
- 未运行 Linux/macOS/Web、MSVC、完整发布矩阵；无远端操作。没有修改 Granit 源码。

构建与验证日志位于本地 build/libassert-*.log，不作为可移植验收产物提交。
