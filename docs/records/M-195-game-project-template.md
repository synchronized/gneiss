<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-195：最小游戏工程模板

## 结论

`templates/game` 提供可复制的最小游戏工程骨架，包括工程描述、Camera 场景、CMake preset 和原生
Game Module。模板只包含 Gneiss 公共头并链接安装包导出的 `gneiss::engine`，不引用源码树、私有头
或 Granit 原生类型。

Game Module 展示初始化、逐帧更新、结构化日志和确定关闭。构建产物写入工程自身的 `modules/`，
Runtime 按 `gneiss.project.json` 中的平台无关模块名加载。

模板明确要求 Shared Engine SDK。Static SDK 适用于直接链接应用，但无法在不复制 Engine 状态的
前提下提供当前原生动态模块闭环，因此配置阶段会给出明确错误。
