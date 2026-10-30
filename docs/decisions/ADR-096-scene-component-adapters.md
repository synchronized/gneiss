<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# ADR-096：Function 层注册描述与运行组件的转换

状态：已接受，实施见 [PLAN-083](../plans/PLAN-083-scene-component-adapters.md)。

## 决策

描述 GUID 索引不可变操作表，包含验证、安装、捕获、删除、比较、覆盖和资源依赖枚举。
首次场景组件操作冻结注册；启动期完成扩展注册。回调在 World 所属线程运行，纯描述操作可并行。
适配负责持久化字段与实际 ECS 数据转换；Meta 不依赖 World，Scene 不解析扩展组件的内存布局。
内建相机的旧属性类型 GUID 在其适配内转换，不散布到通用 Prefab 流程。

## 取舍

比继续增加 switch 更易验证扩展闭环；比完整插件系统范围小。
具体 ECS 存储与生命周期仍属于 World，后端头仅供 World 内部适配实现使用，不经普通接口泄漏。

运行安装失败不得修改目标组件，必须允许使用先前捕获的有效描述恢复；完整重载的回滚由调用方协调。
扩展点用于现有 ECS，不承诺动态卸载插件、跨线程 World 访问或自动生成专用编辑器面板。
