<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：已采用版本选择索引

2026-10-15，按用户确认的方案 A 实施缓存层原语，决策见
[ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。

## 已实现

resource_cache 增加 adopt_versions 和 observe_selected，选择域由来源身份、URI、类型与
调用方编码的兼容准备配置组成。配置为空表示默认域；缓存不理解设备或后端配置语义。
选择只保存指向既有版本条目的键，没有另一套资源对象或强引用。

版本发布不自动改变选择。显式采用只接受已发布且就绪的版本；身份不匹配、重复选择域、
目标版本缺失时整批失败。所有分配与验证完成后才交换选择索引，原选择保持到成功提交。
重复采用相同版本不增加修订号，不改写旧版本或其使用者。release_unused 回收资源后
移除失效选择，默认选择不意味着永久驻留。

本阶段原语使用复制索引后交换，时间与空间开销随选择数量增长，不宣称常量时间提交准备。

## 验证

Windows Clang Debug 相关目标构建及定向 CTest 7/7 通过：resource_service、render_asset_loader、
texture-load-service、scene-load-builder、scene-load-application、core-boundary、core-boundary-negative。
新增覆盖发布不自动采用、显式切换、旧租约保持、重复采用、同批失败无部分提交、
重复域/跨来源拒绝、配置域隔离及资源回收后查询失效。

缓存及测试执行 clang-tidy，新增初始化告警已修正，测试文件既有复杂度等告警保留。
未运行 GPU、完整 Sponza、跨平台或安装矩阵，未执行远端操作。

## 尚未接入

宿主、同步获取和节点创建还未调用选择索引，渲染准备配置的规范编码也未接入。
adopt_versions 仅保证缓存内部原子性，不能在 ECS 已提交后直接调用并忽略分配失败。
下一步须将选择更新的准备与最终提交分开，再与场景租约切换/激活组成一致的事务，
之后普通获取才能读取已采用版本。Application 级共享资产所有权仍待迁移。
