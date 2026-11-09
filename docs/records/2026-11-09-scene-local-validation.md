<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 Scene/Prefab 局部领域校验

## 变更

移除 game_object_description、prefab_reference_description、prefab_property_override_key 和
prefab_component_override 在通用序列化中的 validation 注册。整体场景/Prefab 校验递归检查
局部对象与引用，再检查层级；覆盖键和组件覆盖继续保留独立领域函数，错误路径逐层补齐。
文档记录恢复原有的显式领域校验保留。组件内容和数学值校验尚未在本次迁出。

通用 JSON 允许表达 nil 对象身份、非法 Prefab 来源以及业务上互斥的描述，领域采用必须拒绝；
测试分别断言结构编解码与领域失败，而非把普通 JSON 编码成功当成内容可以采用。
字段类型、required、非法 JSON 容器结构等仍由序列化拒绝，文件入口保留完整诊断路径。

## 验证

定向构建与 scene_description、prefab_description、prefab_property_override、
prefab-preparation 四项 CPU 测试通过。
日志：build/plan108-scene-local-final-build.log、build/plan108-scene-local-rebuild.log、
build/plan108-scene-local-tests.log。
另外运行现有相关测试目标的 45 项 CPU 回归，全部通过；日志为
build/plan108-scene-local-regression.log。本轮未重新构建全部目标。
git diff --check 通过；未执行 GPU、大场景或远端 CI。
