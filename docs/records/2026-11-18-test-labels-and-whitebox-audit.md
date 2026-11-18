<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta、协议测试分类与白盒依赖核查

## 范围与结果

继续 PLAN-119，核查 tests/engine/meta、tests/protocol 与 Prefab 覆盖测试的目标依赖。
本批不改变生产 API，不合并不同所有权语义的测试，也不增加测试数量。

- 13 项 Meta 测试补 cpu 分类：内存反射/序列化为 unit，默认注册表与内建资源装配为
  integration；异常注册子进程另标 death。沿用已有 Fatal 测试入口和终止诊断。
- 10 项协议测试补 cpu 分类：协议/反射编解码装配为 integration，独立 dispatcher 为
  unit。它们使用内存消息，不启动 Runtime、不建立真实 IPC 连接。
- Prefab 覆盖白盒目标删除未使用的 asset_uri.cpp；共享与静态链接、行为测试均通过。
- reflection-json 保留私有 json_adapter.hpp 与 yyjson 依赖：现有用例通过自定义分配器
  验证分配失败抛出 bad_alloc，属于具体内部边界。没有复制适配器实现或新增导出。
- Prefab 覆盖测试仍直接编译 prefab_edit.cpp、prefab_property_data.cpp、
  prefab_property_override.cpp：准备/校验及排序实现尚有未导出入口，不能仅替换为共享
  Engine 链接。下一步需核查正式消费者与编辑器目标组合，不为删除源码条目扩大 DLL 导出。

## 验证

Windows Clang Debug 共享/静态分别重建 gneiss_prefab_property_override_test，通过。
随后分别运行 ctest --preset windows-clang-debug / windows-clang-static-debug
-L "^cpu$" --output-on-failure，均为 38/38 通过，包含 2 项 death 用例。

共享配置重新枚举仍为 233 项，CPU 分类由 15 增至 38，未分类由 211 降至 188。
标签不是覆盖率，不以剩余无 gpu 标签的测试推断 CPU 安全。本批没有运行全部测试、
专门 GPU 验收、Release、MSVC、Linux 或浏览器验证。日志位于 build/audit-label-*.log。

版本级剩余工作仍见 [DEV-052](../plans/DEV-052-architecture-cleanup.md#收尾审计后的明确待办)。
