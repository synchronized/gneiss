<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：反射类型身份与模板入口

状态：实现完成，编译与静态检查通过；运行验证延期。决策见 [ADR-081](../decisions/ADR-081-runtime-type-identity.md)。

目标：区分稳定 GUID 与运行期 C++ 身份，提供模板注册及查询；不引入自动字段反射或新序列化格式。

1. 类型身份改名 guid，字段值类型改名 type_guid；字段定义使用强类型编号与标志。
2. 注册输入增加可选 type_id，建立指向同一记录的运行期索引；冲突拒绝且失败不留下半条登记。
3. 提供 register_type<T>、describe_type<T>；内建 Transform、Camera 使用模板注册。
4. 迁移业务、测试和文档；覆盖两种查询入口、排序后索引、cv/ref、空/未知身份、冲突、冻结与释放边界。
5. 按当前要求只编译及静态检查，测试执行和大场景回归延期。

## 验收状态

Windows Clang Debug 全部启用目标编译通过，新增模板与双身份查询用例、内建组件接入用例已编译。
Core 边界、公共接口、修改文件格式及 diff 空白检查通过。未执行测试、大场景或 CI；并发与失败边界的
运行结果仍待架构统一回归。当前 API 契约见 [反射参考](../reference/reflection.md)。
