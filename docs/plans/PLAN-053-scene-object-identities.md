<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景对象身份与资源 RID 分离

状态：实现与编译、静态验收完成；运行验收按用户要求暂缓。决策见 [ADR-086](../decisions/ADR-086-scene-object-identities.md)。

## 目标与已确认决策

World 由场景域独占拥有，内部通过 world 引用访问；移除全局 World RID 注册表、借用登记及转发入口。
场景实例使用 scene_instance_id，节点统一使用已有 scene_node_id，Prefab 刷新使用独立 token。
保留实例与节点的所属域、代次与失效检查；资源 RID 继续用于 Mesh、Material、Texture。
编辑器或 IPC 的持久定位仍使用已有 UUID/会话边界，不跨进程传递对象地址。

## 实施与验收

1. 迁移 World 创建、反射、Prefab 覆盖和宿主属性访问，保留所属线程检查与正确析构顺序。
2. 将场景实例入口和存储迁移为强类型 ID；节点内部移除 rid 转换。
3. 将刷新 token 独立，迁移 Runtime、Editor、Host、测试与文档调用方。
4. 验证编译期不能混用标识、失效/跨域拒绝及对象生命周期相关测试代码。
5. 全工程编译、格式和边界检查；按用户要求不执行运行测试、大场景或 CI。

不修改渲染资源所有权、GPU 生命周期、场景文件格式或编辑器撤销行为。

## 实施结果

上述迁移已完成；完整开发 preset 编译（含示例与测试目标）、格式、Core/公共接口边界和差异检查通过。测试仅编译，未执行。详见[实施记录](../records/scene-object-identities-2026-10-26.md)。
