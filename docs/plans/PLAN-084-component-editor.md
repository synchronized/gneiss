<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-084：注册组件的编辑闭环

状态：已完成。延续 [ADR-096](../decisions/ADR-096-scene-component-adapters.md)。

1. 捕获实际组件并通过 meta::instance/type 绘制，沿用现有反射控件。
2. 候选副本编辑后提交到 Scene；历史按稳定身份重定位。Prefab 保存稀疏叶属性覆盖，
   为已有对象、序列元素补充覆盖路径，不支持改变容器结构或增删组件。
3. 测试扩展 ECS 组件的面板、枚举、嵌套属性、撤销重做、保存往返及 Prefab 恢复来源。

资源 URI 保留异步加载入口；主相机状态保留既有只读约束。不建立第二份可变组件表。
验收采用 Debug 编译与相关 CPU 测试，不运行大场景、GPU、远端 CI。

## 验收结果

- 属性面板从实际 ECS 捕获注册组件，按 Meta 类型绘制，保留相机状态与资产异步入口的专用规则。
- 通用编辑候选、Scene 提交、稳定身份历史及 Prefab 叶属性路径已打通。
- 共用 movement 测试组件覆盖枚举、嵌套对象、序列元素、实际 Restore Source 按钮、
  连续编辑合并、撤销/重做、无效值拒绝、保存往返、Prefab 再实例化及已删除目标拒绝。
- 跨字段覆盖整体校验、无效持久化路径和越界下标已覆盖。
- Windows Clang Debug 全部目标编译通过，30 项相关 CPU 回归通过，`git diff --check` 通过。
  未运行大场景、GPU、浏览器或远端 CI。

当前接口和限制见[场景实例参考](../reference/scene-instance.md)，文件字段见
[场景格式](../reference/scene-format.md)。
