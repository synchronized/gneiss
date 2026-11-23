<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 序列化策略拆分与默认字段遍历

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 5 阶段。
删除 binding.hpp、binding 和 find_serialization；内容 codec、引用表示、对象选项及校验
分别作为扩展存储，Serialization 通过 find_policies 组合只读查询结果。
Meta registration 删除 serialization、polymorphic、validate 快捷方法，改用通用 metadata/extend；
可默认构造的类型自动登记构造能力，注册字段即可序列化，不再保存 reflected 开关。
注册器、类型定义、注册表和实例视图禁止反向依赖序列化，新增正反向边界检查。
所有代码、测试与示例调用已迁移；实际 API 见[序列化参考](../reference/serialization.md)。

## 语义

自定义 codec 按方向优先，未定义方向回退到字段遍历；缺少构造能力仍拒绝默认解码。
未知类型、错误类型、多态 GUID、引用环和空引用规则保持。
仅有特殊 codec、没有字段的对象快照仍返回 unsupported，避免保存为空对象丢失状态。
对象校验暂保留既有时机；领域校验分类和字段策略优先级尚未完成。

## 验证

默认 Windows Clang Debug 全量构建通过，日志 build/plan108-policy-split-full-build.log。
最初定向编译发现空模板函数指针的警告及残留 reflected 访问，修复后全量构建通过，未降低警告等级。
45 项 CPU 回归首轮 44 项通过；serialization 的旧断言要求未开启反射即 unsupported。
将该断言改为只注册字段的编解码往返后，定向补编及该测试通过。
日志 build/plan108-policy-split-tests.log、build/plan108-policy-default-build.log、
build/plan108-policy-default-tests.log。仅修改测试断言后不重复已通过的 44 项。
独立新增边界测试也通过，日志 build/plan108-policy-boundary-tests.log。
clang-format 差异检查与 git diff --check 通过；未执行 GPU、大场景或远端 CI。

## 剩余

字段策略覆盖、旧 property_flags 清理、领域校验归属、context 拆分和最终布局仍需推进。
Meta 内置及数学注册实现中的消费策略组合后续移出纯 Meta 实现目录。
