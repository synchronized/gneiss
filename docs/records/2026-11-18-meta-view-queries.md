<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 视图无失败查询收敛

## 范围

接续 [PLAN-118](../plans/PLAN-118-error-contracts.md) P2。
当前规则见[容器视图](../reference/reflection-containers.md)及[反射参考](../reference/reflection.md)。

## 改动与边界

- sequence/associative 的 size 直接返回 std::size_t。
- optional/reference 的 has_value 直接返回 bool；reference owner 直接返回 ref<const void>。
  空容器返回零，空引用返回空强引用；显式保存非空 owner 继续保活目标。
- 上述底层 ops 不返回错误，删除视图内部及 JSON、二进制共用遍历、value 快照和属性面板
  中不可能触发的 result 分支。同一引用读取复用已取得的视图，移除 actual_type 的临时 result 包装。
- at/find/get、关联 keys 的文本校验、variant valueless、能力缺失和自定义替换失败仍保留诊断。
  不改变借用有效期、同步责任、引用持有方式或异常传播；未声明所有查询 noexcept。
- 直接迁移既有测试，补充空 owner 及关联 size 的断言，不引入新测试目标或便利包装。

## 验证

Windows Clang Debug 八个测试目标与 gneiss_editor_ui 构建通过；八项回归通过：
assets.material、type_builder、serialization-archive、reflection-json、meta-instance、
meta-container、meta-type-ops、scene_description。git diff --check 通过。

本批未运行 MSVC Release、全仓/跨平台矩阵、GPU 或交互式面板验收；前批结果不替代本批验证。

## 后续

本批仅完成无失败查询，P2 其余操作契约仍需复核；不据此关闭整个计划。
