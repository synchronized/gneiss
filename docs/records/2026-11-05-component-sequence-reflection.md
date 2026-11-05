<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 组件集合序列反射验收

component_descriptions 通过 sequence_traits 提供只读序列访问及整组 replace，
object_description.components 直接注册成员，删除专用 getter/setter。
通用序列解码先初始化临时 vector，再由 replace 检查非空和类型唯一；失败保留原集合。
不开放组件集合的可写元素或扩容接口，普通 vector 与 array 行为保持不变。

删除 read_components/write_components；IPC 直接使用 Meta 视图 DOM 编解码，
Prefab 新增组件直接编码多态引用，不再构造单元素数组后取出。同步调整宿主内部头文件白名单。
文件和 IPC 的多态信封格式不变。当前扩展约定见
[反射容器视图](../reference/reflection-containers.md#操作表与自定义适配)。

Windows Clang Debug 全量构建、17 项相关测试和核心分层检查通过。
测试覆盖组件集合直接往返、空项和重复类型拒绝、失败保留旧值、受限修改、IPC、
Prefab 覆盖、通用容器及编辑器组件操作。git diff --check 通过。
未运行大场景、完整测试矩阵或远端 CI。
