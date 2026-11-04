<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-074：Camera 描述编辑闭环

状态：已完成。

使用共用反射控件编辑 Camera 描述的 settings；主相机保持只读，不改变独立的场景操作。修改请求包含稳定对象/Prefab 来源身份、组件类型和属性路径，候选副本校验后通过现有场景入口及历史提交。

普通对象保存组件描述；Prefab 实例通过现有 Camera 字段覆盖保存差异，恢复来源值删除覆盖，不修改来源资源。仅接入 Camera，不扩展动态容器或实时 GPU 功能。

验收：编译、描述字段与嵌套替换、错误值、撤销重做、普通场景保存重载、Prefab 覆盖保存重载及来源不变；不运行大场景/远端 CI。

## 实施结果

- Camera 描述使用共用反射树展示 settings；普通节点与 Prefab 节点共用入口，旧平面 Camera 控件在作者 Inspector 中隐藏。
- `edit_component_description` 目前仅接受 Camera 的 settings 路径，拒绝 is_primary 修改。历史按普通 UUID 或 Prefab instance/source 二元身份重查对象，不借用节点或字段地址。
- 候选描述通过反射序列化执行完整相机校验，合法后作为一次历史操作提交；主相机切换不纳入参数历史。
- `scene_instance::set_camera` 支持 Prefab 已有相机的参数覆盖。沿用三个稳定 Camera 属性键，无需改变文件 Schema；恢复来源值会删除对应覆盖。

## 验收

2026-10-30：完整 Windows Clang Debug 构建通过，相关回归测试 19/19 通过。
覆盖参数编辑、无效 near/far 拒绝、整体替换与类型检查、撤销重做不切换主相机、普通组件及 Prefab 覆盖序列化后重新实例化、源 Prefab 不变、覆盖清除和目标销毁后拒绝旧历史。
Camera 反射树已做无窗口 ImGui 绘制验证；未运行实际 GPU 窗口交互、大场景、浏览器或远端 CI。

限制：其他组件仍使用原编辑入口；不支持在 Prefab 来源节点新增/删除 Camera，不增加任意组件的属性路径覆盖格式。
