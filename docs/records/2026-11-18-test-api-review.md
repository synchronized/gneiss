<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 测试组件读取 API 审查

## 已完成修改

核查 scene 与编辑属性测试发现 read_component_value 仍把 try_component 的借用结果转换成
错误码和输出参数，并用成员名探测选择 settings/value。这不是夹具或断言，而是旧接口形态的
适配。删除该辅助及全部四处调用，三个测试直接检查借用并读取 camera_component::settings。

- native-world-lifecycle：直接验证新增相机的值，随后仍验证跨 World、移除与活动相机生命周期。
- component_reflection：保留反射编辑链；组件替换后重新借用，分别检查成功结果与失败保留值。
- editor.author-property-edit：局部读取返回设置副本，不跨撤销/重做持有组件指针。

正式 try_component API 已足够，本轮不增加生产接口，也不创建替代测试包装。

## 重复覆盖判断

以上三处都有相机值检查，但分别验证生命周期、反射修改及编辑历史，不能仅因读取相同字段
就合并。保留原有行为断言，删除的是接口绕路，不宣称删除了重复行为用例。
Prefab 覆盖用例的路径稳定性、候选隔离和覆盖应用尚需与 Meta/编辑器用例逐项比较，
本轮没有完成整个 tests 目录的重复性审核。value_result.hpp 中其他输出参数适配仍待按
真实调用逐项迁移，不将其整体替换为另一套隐藏失败的辅助。

## 验证

Windows Clang Debug 共享、静态分别构建上述三个目标；对应三项测试均通过。
全仓搜索已无 read_component_value。未运行全量、GPU、跨平台或发布验收。
版本任务仍由 [PLAN-119](../plans/PLAN-119-engine-build-test-layout.md) 承接。

## World 创建与对象调用续审

world_service_test 与 native_world_lifecycle_test 已移除 value_result.hpp 依赖。
create_native 直接检查结果并移动所有权；已成功创建的对象直接获取 entity，
变换操作直接使用 game_object，删除旧 node 变量及嵌套 entity_id 转换。

合并 world_service_test::native_operations 中重复的 world::object 跨线程错误检查：
同文件 scene_object_storage 仍覆盖该错误，主函数保留所有者移动后的线程边界验证，
native_world_lifecycle 也保留生命周期流程中的跨线程检查。count/flush 的断言子进程
测试不变，未用查询错误测试替代断言契约测试。

两个目标在共享/静态 Windows Clang Debug 下构建通过；native-world-lifecycle、
world-service-boundary、world-thread-contract 三项在两种配置下均通过。
只格式化受修改的两个测试源文件，未调整生产 API、超时或测试启用条件。
