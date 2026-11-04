<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CPU 资源职责盘点与输入资源迁移

2026-10-21，0.51 特性分支；对应[模块布局计划](../plans/PLAN-051-module-layout.md)批次 B。

## 当前盘点

| 类型 | CPU 描述与准备 | 持有和缓存现状 | 剩余边界 |
| --- | --- | --- | --- |
| 输入映射 | asset/action_map.* 与 caors_action_map_loader | asset_manager::resource_state 的 CAORS manager/cache | 同步准备尚未统一缓存发布 |
| Mesh | asset/mesh_data、asset_preparation | cpu_document_cache 内 mesh_documents 版本弱引用表，CAORS handle 保活数组 | 尚未并入统一 manager；渲染租约另持 RID |
| Material | asset/material_parameters、caors_material_document | material_document_versions 独立版本表，CAORS handle 保活文档 | Render material_resource 含纹理 RID，不可冒充 CPU 资源描述 |
| Texture | asset/texture_preparation_data、texture_payload_source | 准备数据与可重建载荷来源；Render 缓存持有渲染资源租约 | 设备配置、上传确认、弱载荷持有仍需由 Render 收敛 |
| Prefab | function/scene/prefab_description | prefab_asset_loader 使用旧 resource_cache | 描述/解码与实例化须继续分离 |
| Scene | function/scene/scene_description | 场景加载流程持有候选，不据此宣称有统一 CAORS Scene 缓存 | 运行时节点与可重用描述的接口仍待收敛 |

asset_manager 同时拥有 resource_state、resource_cache 和 cpu_document_cache。
因此“已使用 CAORS”不代表“全部资源已由一个 CAORS manager 管理”。GPU RID 租约缓存与
CPU 文档缓存承担不同寿命，后续应分清职责，而非直接把两个表机械合并。

## 本批落地

- action_map、action_definition、action_binding 改归 asset_internal。
- 将 action_map 读取、解析、验证，以及 CAORS 类型与 Loader 的 6 个文件迁到 engine/asset。
  删除 function/input 中旧文件，不留转发兼容头。
- action_id 与 action_state 归 core/input/input_types，保留输入服务代次及实时状态语义。
- input_service 显式消费资产层 action_map；资产请求接口不再依赖 function/input。
- 增加输入服务依赖门禁与负向用例：拒绝 CAORS、VFS、平台适配和 asset_manager，允许资产值。
- 更新 Engine、集成探针、私有头清单及测试源码列表；未新增资源缓存或运行时协调对象。

现阶段仍使用现有 engine/asset 作为 CPU 资源位置；与 function/asset 合并为 resource
属于后续批次，不提前建立平行目录。集成探针的私有静态测试目标仍组装多个模块，
不能将该测试目标当成 Input 运行时拥有 CAORS 的依据。

## 验证范围

Windows Clang Debug 相关目标构建成功，9 项测试通过：CAORS Application、asset probe、
VFS bridge、input_service、action_map、cpp_input、scene-load-builder 及两项架构门禁。
测试覆盖既有成功、失败保留原映射、来源变化、取消、线程池及协作执行路径。
原生无线程静态构建 sc-mat-s 的 CAORS Application 构建及测试也通过。

clang-tidy 已检查输入读取、解析、验证和运行时服务；仍报告迁移代码中已有的 6 项
可读性建议（大括号、all_of 与尾逗号），未关闭规则或宣称零告警。
本批不更改公共 ABI；完整平台与安装矩阵未执行。

## 后续

继续盘点 Prefab/Scene 描述中的实例化依赖，再拆出 CPU 资源契约；缓存收敛按每种资源
的身份、固定版本与取消语义推进。不把这次输入资源迁移等同于批次 B 或版本完成。
