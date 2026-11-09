<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 冻结查询索引

## 范围

2026-11-09，继续 [PLAN-108](../plans/PLAN-108-meta-serialization-api.md) 第 4 阶段。
冻结后的已注册对象按 GUID 排序二分查询，运行期 ID 使用已有索引。
属性 ID 复用排序后的记录数组，名称通过独立排序的槽位数组二分查询；
实例 member(name)、属性访问器与继承目标定位使用相同冻结记录。
继承合并、排序和名称索引分配均先在临时存储完成，再提交冻结状态；
冻结失败仍允许补齐类型并重试，不新增注册表或业务对象所有者。
基础值、枚举与容器类型的既有解析路径保留，不宣称全部类型查询均已索引化。

## 验证

默认 Windows Clang Debug 构建通过，日志 build/plan108-index-build.log。
首轮 40 项 CPU 回归中 39 项通过；新增索引用例遗漏字段类型 GUID，注册阶段失败。
修正测试数据后补编通过（build/plan108-index-verified-build.log），
重跑 gneiss.type_registry 通过（build/plan108-index-verified-tests.log）。
除测试数据外未再修改实现，未重复运行已通过的 39 项。
新增用例覆盖 128 个乱序字段的 ID/名称查找和未命中边界；
既有回归覆盖冻结失败重试、继承、只读、getter/setter、序列化和编辑器访问。
格式检查、git diff --check 通过；未执行 GPU、大场景、完整平台矩阵或远端 CI。

## 后续

无 GUID 对象注册需要同步调整实例父类型定位、继承和序列化身份校验，尚未实施。
第 4 阶段仍有上述能力与内部访问边界清理；策略及 context 拆分也未完成。
