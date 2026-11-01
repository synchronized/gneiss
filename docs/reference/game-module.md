<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 游戏逻辑接入状态

动态 Game Module 已撤销：不再提供模块查询入口、生命周期 C 回调、Game Context 或动态库加载。
Runtime 当前负责场景、输入、渲染与编辑 IPC；Editor 运行直接启动 Runtime，不配置或编译游戏 DLL。
工程模板只包含工程描述与场景资产，模块专用示例全部删除。

客户注册 system、脚本和其他游戏逻辑接入方式均待后续设计，当前没有对应公共注册接口。
决策见 [ADR-082](../decisions/ADR-082-remove-dynamic-game-module.md)。
