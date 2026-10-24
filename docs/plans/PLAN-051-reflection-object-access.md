<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：反射直接对象访问

状态：实现完成，编译和静态检查通过；功能回归延期。

基础反射借用对象指针，删除 World/Entity target 和独立候选 setter；World 层负责定位、线程检查、
复制组件值及提交，Editor、Runtime 和 Prefab 迁移到对应入口。保留稳定 GUID、字段与文件协议。
Camera 的活动状态是 World 派生值，使用包含 settings 和 is_primary 的属性快照；不把 World 引用带入 Core。

实施：调整 Core API，迁移组件访问器与 World 适配，再迁移业务及测试。补充直接对象、空指针、
失败不提交、跨 World/线程和有效性边界。仅编译与静态检查，测试执行及大场景回归延期。

## 验收

Windows Clang Debug 全部启用目标（Engine、Runtime、Editor、示例与测试）编译通过。
Core 边界、公共接口、修改文件格式和 diff 空白检查通过。源码中不再保留 property_target、
candidate_property_setter、candidate_setter 或 set_candidate_property。
已迁移并编译多对象访问、空指针、字段校验、异常、只读字段、World/Entity 失效、跨 World、
跨线程和失败不提交的用例；按要求未执行测试、图形交互、大场景或 CI。
当前接口见 [反射参考](../reference/reflection.md)。
