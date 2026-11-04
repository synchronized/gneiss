<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-062：Prefab 显式根对象

## 状态

已完成。

## 决策与范围

Prefab 描述新增 guid root_uuid，表示对象树入口；uuid 继续表示资产身份。
保留扁平对象表与 parent_uuid，不增加另一套树或递归所有权。
未发布 Prefab v1 格式直接增加必填 root_uuid，不兼容缺失字段的旧描述。

## 实施与验收

- [x] 编解码验证根非零、存在、无父，唯一根及全树连通；拒绝重复 UUID、缺失父节点和环。
- [x] 实例化、根变换覆盖、重载及编辑器生成/解包通过显式身份定位根。
- [x] 更新仓库资产和测试；验证数组重排不改变根、根修改保存及非法结构拒绝。
- [x] Debug 完整编译及相关短测试通过，不运行大场景、GPU 和远端 CI。

当前格式规则维护于 [场景格式参考](../reference/scene-format.md#prefab-根语义)。

## 验收记录

2026-10-29：Windows Clang Debug 完整编译通过，20 项场景、Prefab 和编辑器相关短测试通过。
新增检查覆盖显式根往返、根身份修改保存、缺失/nil/无效/不存在/有父节点的根拒绝、
循环和重复身份拒绝，以及数组重排后的实例化与重载身份保持；更换根仍要求重新实例化。
仓库作者资产、动态测试夹具和编辑器 Prefab 生成均已写入 root_uuid。
未运行大场景、GPU、浏览器构建和远端 CI；外部未发布旧资产需要补充 root_uuid。
