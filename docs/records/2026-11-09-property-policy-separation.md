<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 属性访问、持久化与编辑策略拆分

## 变更

删除 meta::property_flags 与 property::flags()。注册时以 property_access 指定内存读写约束，
消费侧查询 capabilities。序列化 required/enum_name 迁移到 serialization::field_policy，
编辑器和 Prefab 覆盖限制迁移到 core/property_edit_policy.hpp 的不透明扩展。
Meta 注册器不解释这些消费策略。

字段缺失、枚举名称和只读字段的文件行为保持不变。只读且必填、非枚举字段使用枚举名称的
冲突在序列化入口返回 invalid_argument，路径定位字段；注册本身允许保存不透明扩展。
注册与引用格式保持独立，字段引用策略覆盖尚未实现。

生产、测试、示例中的注册与查询调用同步迁移，独立接口头清单补充 property_edit_policy.hpp。
组件主相机字段仍允许文件恢复，但普通编辑和覆盖被拒绝。

## 验证

定向反射 JSON 构建与测试通过，新增冲突策略双向编解码与错误路径断言。
windows-clang-debug 全目标构建通过；45 项 Meta、序列化、场景/Prefab、编辑器和边界相关 CPU 回归全部通过。
构建日志：build/plan108-property-strategies-full-build.log；测试日志：build/plan108-property-strategies-tests.log。
受影响源码格式检查、旧标志残留检索和 git diff --check 通过。
未执行 GPU、大场景或远端 CI。
