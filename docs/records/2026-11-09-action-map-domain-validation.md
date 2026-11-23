<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 输入映射领域校验边界

## 变更

移除 action_binding 与 action_map_description 的通用序列化校验注册。
validate_action_map 统一检查动作名称、重复名称、绑定、物理键与 scale，返回带路径的诊断。
asset_file 文件读写、input_service 采用与 action_map_loader 自定义解码结果均显式检查。
非法自定义解码结果不创建/替换资产内容；现有重载失败保留旧内容行为不变。

普通 JSON 内容编解码允许尚未满足输入业务规则的描述；required、数值表示范围、重复键和
未知字段等文件结构策略继续生效。没有混入 PLAN-109 异常边界行为变更。

## 验证

定向构建 gneiss_action_map_test、gneiss_input_service_test 与 gneiss_cpp_input_test 通过。
对应 3 项 CPU 测试通过，覆盖文件往返、非法输入、重载失败保留旧内容、通用 JSON 与领域
入口分离、自定义解码器不能绕过校验。
日志：build/plan108-action-domain-build.log、build/plan108-action-input-build.log、
build/plan108-action-domain-tests.log。
未执行全量构建、GPU、大场景或远端 CI。
