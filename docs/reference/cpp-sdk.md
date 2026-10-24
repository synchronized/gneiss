<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ 调用边界与共享值类型

当前不提供公共 C++ 对象 SDK 或公共 C++ 值头。引擎、Runtime、Editor 使用 src 原生接口，
内部头不安装、不承诺兼容。游戏动态模块使用 [Game Module 协议](game-module.md)。

实体 ID、场景节点 ID 和反射共享值分别位于 `src/engine/core/entity_id.hpp`、
`src/engine/core/scene_types.hpp` 和 `src/engine/core/reflection/reflection_types.hpp`。
前两者以固定宽度整数存储，不依赖 C ABI 定义。操作结果使用 `gneiss::engine::result`。

旧 `src/engine/values` 场景转换和结果别名已删除。内部函数直接传递原生值，
C 布局转换只保留在游戏模块协议边界。

Application、Input、Render、World 与 Scene C 操作入口已经撤销；导出清单只保留
游戏模块相关函数。安装头限制为模块协议闭包，生成版本头只供内部使用。验证状态见
[PLAN-051](../plans/PLAN-051-minimal-game-module-api.md)。
