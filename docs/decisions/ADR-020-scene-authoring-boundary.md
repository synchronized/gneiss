<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-020：Editor 场景创作与 Runtime 投影边界

## 状态

已接受。

## 背景

0.8.0 已能把导入资产放入场景，但创建接口仅覆盖 Mesh Renderer 根节点，删除只覆盖叶节点，命令
历史仍由 UI 组合回调。继续增加重命名、重挂接、复制子树、组件增删和场景切换时，如果 Editor
直接修改 JSON、World 或私有 ECS 组件，会形成多个作者事实来源，并使撤销、失败回滚和保存结果
无法保持一致。

## 决策

- Scene Instance 继续同时持有作者描述与其 Runtime 投影，是打开场景期间唯一可写边界。
- 作者修改通过 C11 Scene Instance API 表达，C++20 只提供轻量包装；接口不包含 ImGui、命令对象、
  JSON DOM、STL、EnTT 或 Granit 类型。
- 稳定 UUID 是命令、选择恢复和持久化引用的身份；Entity ID、Scene Node ID、组件地址和 RID 只在
  当前投影中有效，不进入命令快照。
- 创建、重命名、重挂接、组件增删和子树恢复先完成输入与资源校验，再提交作者描述和 Runtime；
  可预期失败保留修改前状态。
- Editor 命令层保存版本内受控的值快照，并在执行时通过 UUID 解析当前 Runtime ID。连续属性编辑
  可以合并为一个命令；失败命令不移动历史位置。
- 首批组件创作只覆盖当前已有真实场景需求的 Transform、Camera 和 Mesh Renderer。反射负责字段
  元数据与值访问，不隐式承担任意组件的构造、销毁或 JSON Schema 生成。
- 面板布局、展开状态、当前选择和视口工具模式属于 Editor UI 状态，不写入 Runtime 场景格式。

## 影响

- 保存始终从一个同步场景实例生成，Editor 不需要维护平行 JSON 或 ECS 状态。
- 节点重建后 Runtime ID 可以改变，后续命令仍可通过 UUID 正确解析目标。
- 增加一种可创作组件时，需要同步定义作者 Schema、Scene Instance 修改接口、运行时投影、撤销快照
  和迁移/测试，不能只在 Inspector 中增加按钮。
- 通用 Prefab、脚本组件和第三方组件工厂仍需独立设计，不由本决策提前冻结。

## 未采用方案

- Editor 直接修改 JSON 后整场重载：拒绝，交互延迟高且难以保持选择、资源租约和失败原子性。
- Editor 直接操作私有 ECS：拒绝，会绕过作者描述并使保存结果丢失修改。
- 把命令系统放入 Runtime 公共 ABI：拒绝，Undo/Redo 是工具策略，不是运行时资源语义。
