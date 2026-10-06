<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-294：Scene 创建参数边界

日期：2026-10-06。

## 变更

普通节点、Mesh Renderer 节点与 Prefab 实例创建改为内部 `node_creation`、
`mesh_renderer_node_creation` 和 `prefab_creation`。字符串使用创建期间有效的借用视图，
场景成功创建后独立拥有作者数据；不保存调用方描述或字符串指针。

C 入口保留原有布局、指针/长度、应用验证与错误顺序，并转换为内部参数。UUID 唯一性、父子关系、
资产获取与失败回滚仍由 Scene 管理。Mesh Renderer 节点仍以单位变换创建，不扩展公共功能。
边界检查增加三类旧 C 描述的负例，禁止 Scene 实现重新消费它们。

## 验证

- 共享完整构建通过；场景、Prefab、作者编辑、结构热重载与边界相关回归 27/27 通过，14.08 秒。
- C++ 场景实例专项 1/1 通过；新增非法结构尺寸以及创建后修改 UUID/长名称缓冲区仍保留独立副本的断言。
- 静态专项构建及 Scene、Prefab、刷新令牌、结构热重载与边界正反例 9/9 通过，4.09 秒。
- clang-tidy 检查 C 入口，既有同类型相邻句柄参数提示保留；没有修改 ABI 来规避提示。

场景查询输出与相机描述尚待收口，版本未完成最终平台矩阵或发布。
