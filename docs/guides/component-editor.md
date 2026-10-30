<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# 编辑 Camera 组件描述

在作者场景中选择带 Camera 的普通对象或 Prefab 节点，Inspector 显示反射生成的 Camera 树。
展开 settings，可编辑 fov（弧度）、near 和 far。is_primary 显示当前活动相机状态，保持只读。

修改先应用于独立描述副本，并校验完整相机参数；非法值保持原状态，不增加历史。
使用场景原有撤销/重做入口。同一次字段拖动合并，settings 整体粘贴作为单独操作。
主相机切换是独立操作，参数撤销不会把活动相机切回历史值。

settings 标题的右键菜单可以复制和粘贴相同类型的对象。
相机 settings 没有合法的零初始化默认值，且其字段为必填；通用“恢复默认值”操作会报告错误并保持原值。

普通对象保存修改后的组件描述。Prefab 节点保存相对于来源的参数覆盖，不改写源 Prefab；
恢复某字段的来源值后，对应覆盖会自动删除。保存后重新加载保留这些差异。

当前仅 Camera 接入描述编辑入口。其他组件沿用原面板，Prefab 来源节点不支持在此新增或删除 Camera。
