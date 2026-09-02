<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-118 Prefab 原子实例化与生命周期实施记录

## 结论

M-118 已建立 Prefab 的独立 Runtime 投影对象。每个实例先获取 Prefab 与渲染资产租约，再创建实例
根和源节点子树；提交中任一步失败都会销毁已创建实体与节点并释放租约，不向调用方发布半成品。

## 当前行为

- 实例根保存实例级 Transform，并作为 Prefab 源根的父节点参与现有 Scene Tree TRS 组合。
- 每个源节点创建独立实体和节点，以实例 UUID 与源节点 UUID 的复合地址在实例内查找。
- Mesh Renderer 在节点提交前持有 Mesh 与 Material 租约，Runtime 组件只保存对应 RID。
- Camera 和主 Camera 标记沿用场景实例化行为；Prefab 格式本身不引入后端类型。
- Runtime 投影由 RAII 对象独占；销毁实例后节点 ID 失效，其他实例不受影响。
- 当前对象是 Engine 私有能力，M-119 将通过场景 v3 实例声明把它纳入 Scene Instance 所有权。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 同一 Prefab 创建两个独立 Runtime 实例 | 通过 |
| 两实例根 Transform 分别与源根组合 | 通过 |
| 复合地址查找返回不同节点 ID | 通过 |
| 缺失 Mesh 依赖在创建节点前失败且 World 不变 | 通过 |
| 销毁一个实例不影响另一个实例 | 通过 |
| 销毁后旧节点 ID 失效 | 通过 |
| 非法根 Transform 导致部分提交完整回滚 | 通过 |
| Windows Clang Debug 相关专项测试 | 5/5 通过 |

场景作者数据尚不保存 Prefab 声明，也不会序列化展开节点；该持久化边界属于 M-119。
