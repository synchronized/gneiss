<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-296：World、节点和组件的 SDK 所有权审查

日期：2026-10-06。

## 审查结论

核对 World 包装、C 适配与既有 World/Scene/反射测试，补齐 19 个函数的 C++ 入口、所有权和测试映射。
实体与节点由 World 拥有，ID 是借用标识；Mesh Renderer 只存资源 RID；描述与 Transform 为 C 布局值类型映射。
不新增逐实体或逐节点 RAII 所有者，避免对已由 World 管理的对象形成第二份销毁责任。

保留既有错误输出差异：活动相机查询失败清空 ID，实体局部变换查询先输出单位变换；
部分强类型 ID 与 bool 包装则只在成功时改写输出。契约见 [World](../reference/world.md#c-包装)，本次没有修改运行逻辑或 C ABI。

## 验证

补充 C++ 行为测试覆盖无活动相机、移除与重复移除、旧 Camera 值入口、移动后失效查询；
节点循环拒绝、重挂接、实体 Transform 重载、递归销毁后实体仍存活及旧节点 ID 失效；
渲染组件拒绝零 RID、重复移除和旧实体失败。

资源测试在删除组件/实体后转移并显式销毁原 Mesh/Material，要求销毁实际成功，
没有使用允许旧句柄的 reset 结果代替资源仍存活的证据。

- Windows Clang Debug 共享全量增量构建、静态相关目标构建通过。
- 两种链接方式的 World、Scene、反射与资源所有权专项均 8/8 通过：共享 0.39 秒、静态 0.68 秒。
- clang-format 与 git diff --check 通过。
- 本轮未重跑安装消费者、完整矩阵或远端 Actions；当前变动仅测试与契约注释。

函数语义清单累计 68/102 reviewed，场景实例与其余回调接口继续审查。
