<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ABI 基线

本目录只维护最小游戏模块 C 协议。`baseline/game-module-c.txt` 记录当前模块入口，
用于发现意外增删；当前入口均为 experimental，不承诺原完整 SDK 的兼容性。

基线文件按符号名升序排列，一行一个符号。自动比较、结构布局和常量值验证由 M-67 接入。

`api-stability.txt` 为每个当前导出符号记录 `stable` 或 `experimental`。测试要求清单与基线集合完全
一致，拒绝未知级别、重复、遗漏和基线外符号。

## 当前 C++ 映射清单

`cpp-type-inventory.json` 随当前模块协议头维护，记录原生类型映射与真实 ABI 例外。
0.45 发布时的原始清单仍保留在 `docs/records/artifacts/`，不随目录整理回写历史证据。
