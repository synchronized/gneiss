<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-155：结构热重载边界与现状审计记录

## 结论

现有 Scene 实例已经保存作者描述以及 UUID 到节点、实体和资源租约的映射，可作为身份协调的旧快照；
当前普通 Scene 对象无需新增全局 ECS 组件来源表。对于 Transform、Camera 和 Mesh Renderer，可以由旧
作者描述判断组件是否归作者所有，在保留实体上更新作者组件，并自然保留 Game Module 添加的其他
组件。

现有 Prefab 刷新不满足 0.24.0 的身份要求：`refresh_prefab_instance()` 会创建一份完整的新
`prefab_runtime_instance`，替换智能指针后析构旧投影，因此即使来源节点 UUID 未变化，其节点 ID 和
实体 ID 也会全部变化。结构热重载不能直接复用该替换路径，需要在现有实例内部按复合作者身份协调。

## 可复用能力

- Scene 描述已经严格验证 UUID、层级、组件字段和资源 URI，并由实例保存最后提交版本。
- 普通 Scene 对象保存 UUID、节点 ID、实体 ID、Mesh 与 Material 租约，可直接建立旧身份索引。
- Prefab Runtime 节点保存“实例 UUID + 来源节点 UUID”、节点 ID、实体 ID 和资源租约。
- Prefab Override 已通过 Type ID、Field ID 和复合作者地址严格验证；失效目标会返回错误。
- Asset IPC 已具备修订、请求 ID、能力协商、过期过滤、重连重同步和整批结果。
- Runtime 已在主线程安全点执行资产请求，Editor 已有等待、成功、失败与需要重启状态模型。

## 缺失能力

- `scene_instance_service` 尚无从 URI 重新加载到既有实例的内部入口，也没有候选差异类型。
- Scene 和 Prefab 当前都没有保留匹配实体的结构提交与回滚日志；Prefab 手动刷新会整体重建。
- Asset IPC 的资产类型只有 Texture、Material 和静态 Mesh，Editor 发布器也只识别这三类 URI。
- Runtime 的应用函数只构造渲染资源事务并刷新 Mesh Renderer，不能路由结构修订。
- Editor 保存 Scene/Prefab 与外部文件冲突状态尚未统一接入资产修订发布。

## 实施约束

- 首批只协调现有可序列化组件：Transform、Camera 和 Mesh Renderer。未来增加任意反射组件时，再将
  作者来源元数据推广为通用组件机制。
- 单个 Asset IPC 请求不得混合渲染资源修订与 Scene/Prefab 结构修订。当前一次作者操作天然产生
  同类批次；显式拒绝混合批次可以避免两个独立事务提交器产生半提交。
- Scene 修订首版只允许当前启动场景 URI；其他 Scene 资产变化可记录但不修改当前 Runtime。
- Prefab 修订按 URI 找出当前场景的全部同源实例，并以一个批次准备、提交或回滚。
- 新内部接口放在 Gneiss Engine 实现层，不增加 Stable C ABI，也不需要 Granit 改动。

## 下一步

M-156 先实现不触碰 World 的纯数据身份协调差异，并覆盖确定排序和非法候选；M-157、M-158 再分别
把该差异接入 Scene 与 Prefab 事务，避免在协议层或 Runtime 主循环中堆叠结构算法。
