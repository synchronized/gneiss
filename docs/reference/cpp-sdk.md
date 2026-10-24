<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# C++ 调用边界与共享值类型

当前不提供公共 C++ 对象 SDK 或公共 C++ 值头。引擎、Runtime、Editor 使用 src 原生接口，
内部头不安装、不承诺兼容。游戏动态模块使用 [Game Module 协议](game-module.md)。

实体 ID、场景节点 ID 和反射共享值分别位于 `src/engine/core/entity_id.hpp`、
`src/engine/core/scene_types.hpp` 和 `src/engine/core/reflection/reflection_types.hpp`。
前两者以固定宽度整数存储，不依赖 C ABI 定义。操作结果使用 `gneiss::engine::result`。

`src/engine/values` 暂存迁移中的共享描述及旧 C 值转换，通过内部构建接口提供，
不属于安装包。此处的 `gneiss::result` 只是原生结果的内部别名，没有第二份实现。
这些辅助代码仍依赖部分 C 布局，尚不能视为完整原生化。

完整 C API 及其部分示例当前仍保留，撤销进度见
[PLAN-051](../plans/PLAN-051-minimal-game-module-api.md)。
