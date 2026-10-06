<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-305：移除公共头转发路径

2026-10-06，0.47 特性分支的头路径收口验收。范围以
[VER-047](../plans/VER-047-0.47.0-native-cpp-sdk.md) 为准。

## 变更

删除 26 个根功能/Core 转发头；Engine 规范头与两个根总入口继续安装，共 28 个头。
活动调用方改为规范路径，历史文档及冻结 ABI 快照保持原样。SDK 不增加 Runtime、Editor 或插件接口。
安装消费者配置时分别尝试包含每一个旧 C/C++ 路径；除要求编译失败，还核对缺少头文件的诊断。

## 验证

Windows Clang Debug 共享和静态全量构建通过，包含 C11/C++20 逐头独立编译。
共享边界、API/ABI 和 C++ 包装相关回归 25/25 通过，静态对应回归 24/24 通过。
使用全新 `build/0.47-headers-prefix-shared` 和 `build/0.47-headers-prefix-static` 安装，
26 个旧路径均因找不到头而编译失败；两类安装的 C、C++ 与属性检查消费者各 3/3 通过。

## 限制

本记录只覆盖头路径移除；其余 SDK 类型仍在迁移，未运行整版最终远端矩阵，尚未发布 0.47。
旧安装目录可能残留转发头，不能替代本次干净安装验收。当前路径规则见
[目录参考](../concepts/repository-layout.md#公共头路径)。
