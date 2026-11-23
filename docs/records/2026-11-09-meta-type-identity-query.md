<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 类型身份比较与元素查询

## 变更

element_type 直接返回类型句柄，无元素能力返回空值；引用序列化及属性路径操作保留
unsupported 等操作错误。类型比较使用注册表身份与类型身份，不比较静态模板描述地址；
有运行期 ID 时比较 ID，否则比较 GUID，两个空句柄相等。
跨注册表比较 C++ 身份仍显式使用 id()，不会因身份相同混用不同注册表的元数据。

## 验证

编辑器及定向目标编译通过，5 项 CPU 测试通过：实例、分类 Ops、序列化、嵌套 JSON、组件编辑。
新增同表模板/ID/GUID 查询相等、跨表不相等、未注册类型比较、无效与标量无元素能力测试。
日志：build/plan108-type-identity-query-build.log、build/plan108-type-identity-query-tests.log。
git diff --check 通过；未执行 GPU、大场景或远端操作。
按身份查询关联原生 Ops、实例布局及其余 PLAN-108 工作仍待实施。
