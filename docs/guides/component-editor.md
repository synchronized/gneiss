<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# 编辑组件描述

在作者场景中选择带 Camera 的普通对象或 Prefab 节点，Inspector 显示反射生成的 Camera 树。
展开 settings，可编辑 fov（弧度）、near 和 far。is_primary 显示当前活动相机状态，保持只读。

修改先应用于独立描述副本，并校验完整相机参数；非法值保持原状态，不增加历史。
使用场景原有撤销/重做入口。同一次字段拖动合并，settings 整体粘贴作为单独操作。
主相机切换是独立操作，参数撤销不会把活动相机切回历史值。

settings 标题的右键菜单可以复制和粘贴相同类型的对象。
相机 settings 没有合法的零初始化默认值，且其字段为必填；通用“恢复默认值”操作会报告错误并保持原值。

普通对象保存修改后的组件描述。Prefab 节点保存相对于来源的参数覆盖，不改写源 Prefab；
恢复某字段的来源值后，对应覆盖会自动删除。保存后重新加载保留这些差异。

Transform 和 Camera 共用反射树、候选校验及属性路径历史。Transform 的 translation/scale 为三分量控件，rotation 以欧拉角显示并转换回四元数。
普通节点与 Prefab 根节点修改自身局部变换；Prefab 来源子节点修改生成覆盖。来源子节点的 Restore Translation/Rotation/Scale 和 Restore All Transform 同样走统一历史入口。

连续拖动同一字段合并为一次历史；不同对象、字段或新的拖动分开记录。切换选择后撤销仍定位原对象；非法零缩放不会提交。
Prefab 来源节点不支持在此新增或删除 Camera 或 MeshRenderer。

Transform 仍保存为对象自身的 transform 字段；本次统一编辑入口不改变组件列表或场景文件格式。

## MeshRenderer 资源引用

带 MeshRenderer 的普通节点、Prefab 根及来源子节点显示 Mesh Renderer 反射树。
mesh/material 为资源 URI，修改后按 Enter 提交。也可以在 Asset Browser 中选择网格或材质，
使用应用到所选节点操作；两种方式共用 `editor_asset_edit`。

描述先校验，缺少的资源通过现有异步加载入口准备。准备期间仍使用原资源；成功才更新绑定、
作者描述和撤销历史。失败或取消不改变绑定；编辑目标已删除、场景已切换或原绑定已改变时，
迟到结果不会覆盖当前状态。重复设置相同资源不增加历史。

普通节点保存组件 URI。Prefab 实例保存 mesh_renderer_description 的稀疏字符串覆盖，
恢复源 URI 自动移除该项；其他实例及共享源 Prefab 不变。场景依赖根据实例最终生效的描述收集，
保存重开后恢复覆盖资源。资源替换历史及 Prefab 刷新快照强持有资源，撤销不重新读取文件。

本入口不自动导入源模型，不支持在 Prefab 来源节点新增或删除组件，也不表示 GPU 上传进度。
