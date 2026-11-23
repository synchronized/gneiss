<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 Scene/Prefab 整体校验边界

## 变更

Scene/Prefab 不再在 Meta 扩展中注册整体校验。asset_file::format 增加可选领域校验回调，
文件保存前、加载独立候选后调用；回调校验视图类型，错误仍保留领域路径。
restore_scene_record 与 restore_prefab_record 原有显式校验继续保留，文档采用路径仍经该入口。

普通 JSON 内容读写允许不满足场景整体约束的数据；不能用它代替资源文件加载或文档采用。
局部对象、组件与数学值等校验尚未在本批迁移，未宣称通用遍历已经完全去掉领域校验。

## 验证

Scene/Prefab 定向构建与两个描述测试通过。新增分层测试覆盖 nil 场景身份、缺少 Prefab 根节点、
重复对象：普通内容编解码可完成，文件入口拒绝；文档记录恢复拒绝非法身份与根节点。
windows-clang-debug 全目标构建与 45 项相关 CPU 回归全部通过。
构建日志：build/plan108-domain-root-full-build.log；测试日志：build/plan108-domain-root-tests.log。
格式检查与 git diff --check 通过；未执行 GPU、大场景或远端 CI。
