<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-074：内部统一使用原生结果对象

- 状态：已接受，内部迁移已实施。
- 日期：2026-10-24

## 决策

内部操作结果统一为 src/engine/core/result.hpp 中的 gneiss::engine::core::result。
参考当前 Granit core/result.hpp 的薄值语义：默认 unknown、非负成功、负值失败、
显式布尔判断、相等比较和静态文本。使用 int32_t 存储，不分配内存，不引入 result<T>。
内部文本查询直接调用核心实现，不绕行 C ABI。

C ABI 保留 gneiss_result 与全部既有数值。engine/api/result_conversion.hpp 提供
显式 to_c_result/from_c_result，编译期检查所有已知码，未知码无损往返。
不提供隐式整数转换、跨结果类型比较或公共结果类型的隐式构造。
第三方错误在对应后端映射，不按数值直接转换。

公共 gneiss::result 保留为公共值类型兼容层，公共头不能包含 src。
内部操作不再使用它；公共值布局转换辅助函数的返回值只在转换调用点显式适配。
业务输出结构、任务阶段与详细诊断保持独立，不强行改成通用结果模板。

## 取舍

显式边界增加少量转换代码，但能在编译时暴露混用，不隐藏未迁移调用链。
内部与 ABI 码值对应关系集中检查；稳定 C ABI 不要求内部对象与整数重解释兼容。
普通内部实现不依赖 engine/api 转换头。游戏模块回调和 IPC 编解码在各自边界显式构造
内部结果或调用 code()；反射 C 回调在注册时适配，并保留原回调身份用于重复绑定判等。
宿主与独立底层目标通过受限内部头清单共享结果值，不暴露整个 src 目录。

实施顺序及验证状态见[迁移计划](../plans/PLAN-051-native-result.md)。
