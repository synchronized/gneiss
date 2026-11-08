<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 运行场景成员操作统一验收

## 已完成

删除 find_authored_node/find_ordinary_node 修改路径，统一为 find_member。
Prefab 展开成员支持改名、重挂接、动态子对象、相机设置/移除和独立子树删除。
这些操作只改变 World，不生成来源覆盖，也不修改编辑文档。

## 验证

Windows Clang Debug 默认构建通过；38 项 Scene/Prefab/World/组件相关回归通过。
补充来源成员独立删除断言后，重跑场景实例、Prefab 实例、组件适配 3 项均通过。
未运行 GPU 大场景或远端 CI。

旧作者捕获与来源重载仍使用来源记录，本次不代表 PLAN-105 整体完成。
