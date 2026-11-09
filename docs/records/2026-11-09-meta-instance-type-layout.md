<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 实例类型状态精简

## 实现

const_instance 持有一个 meta::type 和访问位置，删除重复的 registry、GUID、type_id、
类别、标量类别、枚举标记及 Ops 指针。type() 直接返回所持句柄，不重新查表。
容器与引用视图使用同一能力来源；冻结前视图不会自动获取后来发布的元数据。
标量 getter/setter 仍通过属性位置调用，只有直接地址才使用原生标量操作，避免绕过 setter。

本批完成实例的类型状态收敛；可写/只读位置的统一及 dynamic_type 迁移仍待阶段 4b 后续实施。

## 验证

新增实例布局上界静态断言。编辑器与相关目标编译通过，7 项 CPU 测试全部通过：
实例、容器、分类 Ops、序列化、Archive、反射 JSON 和组件编辑。
既有测试覆盖 const 拒绝写、getter、受约束 setter、继承地址调整、引用及句柄生命周期。
日志：build/plan108-instance-type-layout-build.log、build/plan108-instance-type-layout-tests.log。
git diff --check 通过；未执行 GPU、大场景或远端操作。
