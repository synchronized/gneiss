<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 当前兼容性边界

Gneiss 当前处于开发期，不提供完整公共 C/C++ SDK，也不提供动态 Game Module 协议。
旧 SDK 函数、对象包装和模块入口已删除；原消费者不能按旧协议链接或加载。
同源码构建的 Engine、Runtime、Editor、工具与示例使用 src 原生接口，升级时须一起重建。
为共享库链接而导出的内部 C++ 符号不构成稳定 ABI。

当前 include 仅保留符号可见性宏与结果码常量，不包含公共操作函数。
工程与资产文件的格式规则分别见对应 Reference；旧 game_module 工程字段被拒绝，返回 unsupported。
Editor–Runtime IPC 仍是同版本宿主协议，不属于对外扩展 SDK。

客户 system 注册、脚本接入和独立游戏代码分发留待后续设计。
历史版本的兼容审计属于历史记录，不代表当前承诺。
决策见 [ADR-082](../decisions/ADR-082-remove-dynamic-game-module.md)。
