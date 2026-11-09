<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 材质领域校验收敛

## 变更

material_parameters 与 texture_sampling 不再注册通用序列化 validation。
参数值域、有限数值、alpha_mode 和采样约束统一到 resource/material/material_description.cpp。
文件读写使用 validate_material_description，资产内容采用显式调用领域校验，渲染服务调用
validate_material_parameters 并继续独立校验所属服务的纹理 RID。
编辑器既有候选提交仍通过文件边界检查，不因删除反射校验而允许非法值进入历史。

普通 JSON 允许尚未满足材质业务约束的结构；类型表示和字段结构规则保持。
新增测试覆盖非法 roughness 的通用内容往返、文件拒绝和非法采样的资产采用拒绝，保留字段路径。
未调整材质 URI 的既有验证边界，不宣称所有资源领域校验已迁移完成。

## 验证

定向构建 gneiss_render_asset_preparation_test、gneiss_material_editor_test、
gneiss_pbr_material_resource_test 通过；对应 3 项 CPU 测试通过。
PBR 测试只操作资源服务的 CPU 资源状态，没有创建窗口、渲染设备或提交 GPU 帧。
日志：build/plan108-material-domain-build.log、build/plan108-material-domain-tests.log。
git diff --check 通过。未执行全量构建、大场景、GPU 或远端 CI。
