<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 组件与数学值的校验边界

## 实现

资源层定义 component_validation_policy，用 Meta 扩展登记组件领域回调，不新增注册表。
validate_component_description 负责描述有效性及显式调用回调；Scene/Prefab 文件、文档恢复
递归覆盖普通组件与覆盖新增组件。相机及网格适配器复用同一领域校验函数。
数学 Meta 注册删除整体变换与相机领域回调；变换 setter 的访问约束保留，场景保存检查整体变换。
通用相机/组件编解码和快照可表示尚未采用的内容，领域采用边界仍拒绝非法数据。
领域错误路径不再含 JSON 包装字段 $content，指向实际对象/组件位置。

修复引用快照仅检查 encode 的遗漏：自定义 decode 且没有 capture 时，同样拒绝按内嵌内容捕获。
空引用行为保持。

## 验证

编辑器及相关目标编译通过；scene_description、type_registry、reflection-json、component-adapter、
editor.author-property-edit、editor.component-edit 共 6 项 CPU 测试通过。
新增用例验证非法相机与网格可通用编码/捕获、领域校验拒绝、Prefab 新增组件拒绝；
覆盖只有 encode/只有 decode 的引用快照拒绝与空引用成功。
既有场景保存、加载、编辑失败不修改历史与 World 的用例通过。
构建日志：build/plan108-component-validation-build.log、build/plan108-domain-validation-tests-build.log。
未执行 GPU、大场景或远端验证；git diff --check 通过。
