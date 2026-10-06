<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Engine 公共头目录整理验收

2026-10-06，基于已发布 0.45.0 的公共头目录整理，0.46 本地及远端验收已完成，待合并和发布。
本记录分别保留目录初验、首轮失败与最终候选结果。

## 范围

公共功能头及基础类型迁入 `include/gneiss/engine/`，根总入口保留；旧路径仅转发。
生成版本头、安装文件集、示例、当前 API 清单及依赖检查同步适配。
当前清单移至 `abi/cpp-api-inventory.json` 与 `abi/cpp-type-inventory.json`，0.45 历史清单不修改。
不改变函数、类型、ABI、运行时行为；不开发插件、不开放 Editor API。
规则见 [公共头路径](../concepts/repository-layout.md#公共头路径)。

## 目录整理初验

- Windows Clang Debug 共享完整构建通过；静态 Engine 与 C11/C++20 独立头目标构建通过。
- 新路径逐头独立编译，原路径头测试保留；安装后的 C/C++ 混用新旧路径。
- 共享、静态安装消费者各 3/3 通过，包含属性检查器；两种 SDK 均安装预期 54 个自有公共头。
- API 稳定性、内部边界与 C++ 清单正反例共 5 项通过。复用旧构建目录的夹具最初因残留旧版本
  模板失败，改为在确认路径位于构建目录内后重建专用夹具，失败项复验通过。
- C/C++ 版本、结果及既有 ABI 消费者专项 6/6 通过。
- DLL 的 102 个公共 C 符号和 61 个同版本私有 C++ 符号与 0.45 发布前基线逐项一致。
- 全部迁移声明及包装与 main 基线比对，仅包含路径变化；格式与 `git diff --check` 通过。

本地日志位于 `build/include-layout-*`。初验时未运行远端 Actions、完整图形或大场景矩阵，
也未验证 Linux/MSVC；最终平台结果见下文。本次没有渲染及运行逻辑改动。

## 0.46 发布候选验收

2026-10-06，工程版本号已调整为 0.46.0。Windows Clang Debug 共享完整构建通过，
完整 CTest 187/187 通过（210.63 秒）；静态完整构建与 CTest 184/184 通过（135.67 秒）。
最终共享/静态独立安装消费者各 3/3 通过；安装集合各为 54 个自有公共头，版本均为 0.46.0。
远端完整矩阵结果见下文，发布尚未完成；上文是版本号调整前的专项证据。

### 首轮 MSVC 头编译夹具修复

首轮 Windows 安装检查发现 C4206：独立包含仅有宏的 export/version 头时，生成的 C 编译单元为空，
在 `/WX` 下失败。生成夹具增加无运行行为的 typedef，使其成为合法非空单元，仍然只包含被测头。
没有关闭警告、减少头覆盖或修改公共声明。本机 MSVC C11 `/utf-8 /W4 /WX` 的两个宏头编译通过，
Clang 共享/静态全部独立头目标复验通过。生成器为平台共用，修复候选重新验证 Windows/Linux 工作流。

### 首轮 Linux IPC 异常

候选 `77da7a3` 的 [Linux 首轮](https://github.com/synchronized/gneiss/actions/runs/37446399813)
静态图形任务中，`gneiss.runtime.ipc-session` 被 SIGPIPE 终止（其余 181/182 用例通过）。
相关 IPC 传输、会话和会话测试源码与 v0.45.0 无差异；仅凭这点不能确认根因或证明目录调整无关。
失败日志保留在 `build/0.46-linux-first-failed.log`。当前候选正常执行该用例，没有跳过或关闭检查；
后续通过也不表示 SIGPIPE 根因已解决。该异常作为发布风险保留，尚无确定复现条件与修复结论。

### 修复候选平台结果

候选 `be49900` 的 [Linux 37447294275](https://github.com/synchronized/gneiss/actions/runs/37447294275)
8/8 全部通过；图形共享 185/185、静态 182/182，额外 Vulkan validation 像素专项均通过。
两种图形配置的 `runtime.ipc-session` 均通过，但这不构成 SIGPIPE 根因修复证据。
[Windows 37447289209](https://github.com/synchronized/gneiss/actions/runs/37447289209) 4/4 全部通过，
包含共享/静态安装消费者和两种 MSVC 宿主。最终候选合计 12/12 通过。
本地最终日志位于 `build/0.46-*`；生成测试单元修复后单独复验了两种链接配置的全部公共头，
没有修改 Engine 运行逻辑，因此完整本地 CTest 复用版本号调整后的已通过结果。

原始任务身份、候选 SHA、状态及链接见[远端检查摘要](artifacts/0.46-release-checks.json)。
最终合并前仅补充文档与验收记录，不重复运行未变化的代码矩阵。
