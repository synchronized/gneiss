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

## 编辑器接入

[PLAN-084](../plans/PLAN-084-component-editor.md) 沿用该适配入口：只读描述提供 Meta 视图，
可写候选由适配器按实际 C++ 类型复制，编辑完成后校验并安装。组件枚举来自实际 ECS，
不在 Document 建立另一份组件权威数据。Prefab 的叶覆盖使用属性/下标路径持久化，
普通编辑和撤销、重做共享 Scene 提交入口；资源准备仍属于异步资产流程。

[PLAN-085](../plans/PLAN-085-component-lifecycle-editor.md) 在同一注册表增加显式默认工厂及可删除标记。
普通对象的组件增删由 Scene 校验成员归属，历史记录描述而非 ECS 指针；资源组件继续复用资产历史强引用。
不允许 Prefab 根及来源节点组件增删，不新增描述权威副本或通用资源恢复协议。
