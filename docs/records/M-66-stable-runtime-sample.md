<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-66：稳定运行时代表性样例验收记录

## 结论

代表性 Consumer 已使用安装后的 Gneiss 与 Granit package 完成独立配置、构建和三帧图形 smoke。
样例源码只包含公开 Gneiss 头文件并链接 `gneiss::gneiss`，运行时资产复制到独立构建目录或随样例
安装，不依赖引擎私有头、私有 target 或源码树路径。

## 覆盖路径

样例复用 Temple 的唯一资产事实源，覆盖以下真实游戏运行时路径：

- 创建 Granit 平台 Application 并取得其 World。
- 从公开场景格式加载层级、Camera、Mesh Renderer、Mesh、Material 与 Texture。
- 加载动作映射，读取 `A`/`D` 与 `Esc` 动作状态。
- 按帧修改 Camera Scene Node 的局部 Transform。
- 提交三帧图形内容或交互运行，随后卸载场景并确定性销毁 Application。
- 任一阶段失败时向标准错误输出阶段、结果码和稳定结果消息。

样例没有使用 Type Registry、场景作者修改、序列化、UI Draw、Debug Draw、Editor 或资产工具 SDK。
这些能力不属于首批 Stable 运行时的必要集合。

## 安装隔离验证

本地 Windows Clang Debug 验证步骤：

1. 构建并运行根工程 `gneiss.stable_runtime_example` smoke，结果通过。
2. 将 Gneiss 安装到独立 `build/stable-runtime-sdk` 前缀。
3. 将 Fetch 构建的 Granit package 安装到同一前缀。
4. 仅以该前缀配置 `examples/stable_runtime` 独立工程。
5. 独立 Consumer 构建通过，`gneiss.stable_runtime_consumer` smoke 在 0.70 秒内通过。

首次只安装 Gneiss 时，独立配置按预期因缺少 `granitConfig.cmake` 失败。这证明 package 正确声明了
依赖，也暴露出 Fetch 场景尚缺自动安装 Granit 到验收前缀的流程；该项转入 M-68，不由样例隐藏。

## Stable 候选收敛

本样例证明首批 Stable 路径需要 Core、Application、World、Input/Action、Scene Tree/Transform、
Scene Instance 运行时读取、资产 URI/格式 Loader 和不泄漏后端类型的 Granit 平台选择。直接创建
Mesh/Texture/Material 的低层资源接口没有被样例使用，应继续保持候选而非直接冻结。

## 后续

M-67 修正定宽 ABI、结构扩展策略和稳定性机器清单；M-68 将 Gneiss、Granit 与独立 Consumer 的
安装验证接入可重复矩阵，并覆盖 Shared/Static 与 package/fetch 组合。
