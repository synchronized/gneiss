<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ 调用边界

当前不提供公共对象 SDK 或游戏模块 C API。Engine、Runtime、Editor 和工具直接使用 src 原生接口。
内部头不安装、不承诺兼容，`include` 仅暂留符号可见性宏和结果码常量，不提供 C 操作函数。

实体和场景节点 ID、反射共享值使用原生定义；操作结果使用 `gneiss::engine::result`。
Runtime 不再装载游戏 DLL。客户 system 注册与脚本接入尚未设计。
迁移状态见 [撤销模块计划](../plans/PLAN-051-remove-game-module.md)。
