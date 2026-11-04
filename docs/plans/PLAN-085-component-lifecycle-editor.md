<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-085：普通对象组件增删

状态：已完成，延续 [ADR-096](../decisions/ADR-096-scene-component-adapters.md)。

1. 注册表增加显式默认描述工厂、允许删除标记；编辑器按注册信息列举可添加组件。
2. Scene 拒绝重复添加、缺失删除和 Prefab 来源结构修改；Document 与历史使用稳定对象身份。
   无依赖组件同步提交；网格继续沿用异步资产准备及持有实际资源引用的历史，不由 URI 重建资源。
3. 扩展 ECS 测试组件覆盖添加、编辑、删除、撤销重做、保存恢复、实体/组件销毁及错误边界。

不引入 Prefab 组件结构覆盖、动态插件或新的资源所有者。相机加入通用注册入口。
验证采用 Debug 编译与相关 CPU 回归，不运行大场景、GPU、浏览器或远端 CI。

## 实施结果

- 注册默认工厂和删除能力；普通对象面板按 Meta 名称显示 Components 菜单，相机使用通用命令。
- Scene/Document 提供普通对象组件增删，历史按 UUID 重定位并比较删除前描述。
- 新增 `component_lifecycle_test`，覆盖增删、属性编辑、撤销重做、编解码及重新实例化、
  ECS 析构、无历史容量、重复添加、缺失删除、安装失败、跨场景/线程及 Prefab 根/来源子节点拒绝。
- 网格继续使用既有资产选择和资源强引用历史；通用同步命令拒绝带资源依赖的描述。

## 验收记录

2026-10-30：`cmake --build --preset windows-clang-debug -j6` 成功；31 项相关 CPU 回归通过，
包含新增组件生命周期、组件编辑、资产历史、Scene/Prefab、保存和 World/Reflection 边界。
未执行 GPU、浏览器、大场景、远端 CI 或桌面面板人工交互验收。
当前能力边界见[场景实例参考](../reference/scene-instance.md#普通对象组件增删)。
