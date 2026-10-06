<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Engine 公共头目录整理验收

2026-10-06，基于已发布 0.45.0 的独立目录整理，本地验证完成；本记录不代表新版本已发布。

## 范围

公共功能头及基础类型迁入 `include/gneiss/engine/`，根总入口保留；旧路径仅转发。
生成版本头、安装文件集、示例、当前 API 清单及依赖检查同步适配。
当前清单移至 `abi/cpp-api-inventory.json` 与 `abi/cpp-type-inventory.json`，0.45 历史清单不修改。
不改变函数、类型、ABI、运行时行为；不开发插件、不开放 Editor API。
规则见 [公共头路径](../concepts/repository-layout.md#公共头路径)。

## 验证结果

- Windows Clang Debug 共享完整构建通过；静态 Engine 与 C11/C++20 独立头目标构建通过。
- 新路径逐头独立编译，原路径头测试保留；安装后的 C/C++ 混用新旧路径。
- 共享、静态安装消费者各 3/3 通过，包含属性检查器；两种 SDK 均安装预期 54 个自有公共头。
- API 稳定性、内部边界与 C++ 清单正反例共 5 项通过。复用旧构建目录的夹具最初因残留旧版本
  模板失败，改为在确认路径位于构建目录内后重建专用夹具，失败项复验通过。
- C/C++ 版本、结果及既有 ABI 消费者专项 6/6 通过。
- DLL 的 102 个公共 C 符号和 61 个同版本私有 C++ 符号与 0.45 发布前基线逐项一致。
- 全部迁移声明及包装与 main 基线比对，仅包含路径变化；格式与 `git diff --check` 通过。

本地日志位于 `build/include-layout-*`。本次未运行远端 Actions、未重跑完整图形或大场景矩阵，
也未重新验证 Linux/MSVC；本次没有渲染及运行逻辑改动。
