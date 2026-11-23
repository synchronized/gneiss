<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 实例读写 API 迁移

## 范围

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。
旧只读 instance 改为 const_instance，旧 mutable_instance 改为 instance，不提供含义冲突的别名。
模板借用统一 meta::view，属性查询统一 member，写值统一 assign；保留运行期类型擦除 from_object。
所有源码、测试和示例同步迁移；尚未修改 binding、context、文件格式或资产依赖调度。

## 访问规则

- view(T&) 返回 result<instance>，view(const T&) 返回 result<const_instance>。
- 显式 registry 与默认入口一致；临时对象借用被删除的重载拒绝。
- 原先明确只读的调用使用 view(std::as_const(object))，不因重命名获得写权限。
- member 的 getter/setter、继承、只读、借用失效规则保留；as_const 只允许单向转换。
- assign(value) 保留原有写值校验；标量 assign(T) 复用 scalar_ops 的值归一化，
  范围检查由目标写入路径执行；assign(const_instance) 保留同类型对象/引用的复制赋值语义。
- Runtime 属性编辑改用 find_type 与 property_range，删除该入口的复制 type_info 查询。

## 验证

Windows Clang Debug 默认构建及格式化后补编通过，日志 build/plan108-instance-verified-build.log。
35 项 Meta/序列化/Prefab/属性编辑及 IPC CPU 回归全部通过（60.12 秒），日志 build/plan108-instance-tests.log。
格式与 git diff --check 通过；未执行 GPU、大场景、完整平台矩阵或远端 CI。
新增编译期断言验证默认/显式表的返回类型、const 隔离和临时值拒绝；
既有属性句柄测试使用 assign(23) 验证标量便捷入口。

## 剩余范围

底层 describe_type/describe_property 的信息结构仍需收敛；注册 Builder、策略拆分、
上下文与格式入口按后续阶段实施。本批不表示 PLAN-108 或 DEV-052 全部完成。
