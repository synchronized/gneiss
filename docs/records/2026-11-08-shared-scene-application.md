<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 通用有效对象应用入口

承接 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的共享组合阶段。

Engine 新增 scene_application，apply_scene 接收前后有效对象列表、外部维护的地址关联和资源引用。
编辑器删除内部 update_objects 实现，改为组合文档后调用共享入口。
编辑值准备通过可选回调传入；Engine 不依赖文档、历史或编辑器类型，不保留回调。

应用按地址保留存活实体，新增对象先创建再连接层级，消失对象从叶到根移除。
目标层级在修改前检查父地址及循环；拒绝跨 World、非所属线程和重复实体关联。
基线不匹配返回 false，尚未执行修改；执行阶段错误不承诺回滚，调用方必须使同步状态失效。

直接回归覆盖空场景首次应用、子先父后输入、父子反转、变换和改名、实体身份保持、
循环拒绝、线程/World 边界、关联表重复、基线失效、删除及卸载释放。
默认 Windows Clang Debug 构建通过，84 项 CPU 定向回归通过；
更正：新增关联表与基线断言后的构建实际失败（entity_id 没有排序运算符），
当时的命令没有中止后续 CTest，3 项回归运行的是旧产物，不能作为最终验收。
后续已改为对 entity_id 的整数值去重，构建成功后再启动测试；修复后的默认构建及
84 项定向回归全部通过。
未运行 GPU 大场景、远端 CI 或其他平台矩阵。

Runtime 首次实例化尚未全部接入 apply_scene，旧 prefab_binding 没有删除。
组件操作仍可通过资产管理器解析 URI，切换 Runtime 创建前需贯通显式资源引用消费，
再迁移来源重载和作者捕获调用方。本记录不表示 PLAN-105 已完成。
