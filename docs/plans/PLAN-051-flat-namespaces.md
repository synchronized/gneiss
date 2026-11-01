<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：内部命名空间按层收敛

## 状态

已完成迁移。Windows Clang Debug 全目标编译、格式与架构边界检查通过，
测试目标仅编译，未执行测试、
大场景或 CI；性能采集启用配置未单独构建。

## 目标与边界

Engine、Runtime、Editor、Tooling、Host 分别使用 gneiss 对应的第二层命名空间。
保留目录、CMake 目标和依赖方向；公共 C ABI 和公共共享值类型不变。
单文件辅助代码用匿名命名空间，头文件实现细节允许 detail，不保留旧名兼容别名。

## 实施顺序

1. 迁移声明、限定名、前置声明、测试与示例。
2. 删除同一类型的重复别名，按职责处理真正的重名。
3. 更新架构边界检查和当前文档，历史记录保持原样。
4. 格式、边界和全目标编译检查；不执行测试、大场景与 CI。

## 验收

无旧业务子命名空间；分层依赖检查保持有效；编译通过。
内部 C++ 符号改变，内部消费者必须一起重新编译。
World 与反射注册表原有同签名 create/destroy 改为 create_world/destroy_world 和
create_type_registry/destroy_type_registry；同类型跨空间别名删除。
宿主工程、进程和 IPC 类型归 Host，编辑模型继续归 Editor。
