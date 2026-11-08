<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景成员访问与生命周期验收

本记录覆盖 PLAN-105 的场景成员访问入口，不代表整个计划完成。

## 已完成

- 场景集合集中校验所属线程和场景句柄；访问前清除已被 World 删除的成员。
- 非拥有 scene_instance 接口迁到 function/world，增加 create，显式登记动态成员。
- 从 World 删除子树后，场景计数、查询与再次使用相同 UUID 创建不保留失效记录。
- 动态成员脱离父节点后仍受场景卸载管理；错误线程的改名不改变对象。

## 验证

Windows Clang Debug 默认构建通过；Scene、Prefab、World、组件、编辑器、渲染快照及边界相关
84 项 CPU 回归全部通过（79.75 秒）。未运行 GPU 大场景或远端 CI。

运行层 Prefab 来源记录及专用结构更新路径尚待迁移，见
[PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。
