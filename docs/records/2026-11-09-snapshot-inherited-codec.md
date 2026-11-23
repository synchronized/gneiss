<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 自定义 codec 继承字段快照修复

扩大 PLAN-108 回归时，component-description 测试暴露：自定义组件 codec 没有注册自身字段，
但继承了 UUID，旧的“属性列表非空”检查错误地允许字段快照，丢失组件实际内容。
捕获和恢复现在共用规则：存在自定义内容 codec 时，必须至少注册自身字段才能走字段快照；
仅有继承字段返回 unsupported。已注册自身字段的类型仍由注册者保证快照数据完整。

既有组件编辑拒绝测试恢复通过，新增不完整 record 恢复拒绝测试。
组件描述、序列化、Archive、反射 JSON 和容器定向测试通过。
构建日志：build/plan108-snapshot-codec-build.log。
未执行 GPU、大场景或远端操作；git diff --check 通过。
