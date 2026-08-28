<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-65：公共 API 与稳定性审计记录

## 结论

截至 2026-08-28，Windows Clang Shared 构建导出 83 个 `gneiss_` C 符号，公共安装面包含 12 个
C 头及对应 C++20 包装。当前接口足以进入 1.0 加固，但尚不能整体冻结：C `enum` 的定宽 ABI、部分
查询结构的扩展策略及实验能力标记必须先完成。

当前符号快照保存在 [`abi/baseline/gneiss-1.0.0-c.txt`](../../abi/baseline/gneiss-1.0.0-c.txt)。
该文件是 1.0 开发基线，不是最终 Stable 清单。

## 模块审计

| 公共模块 | 当前判断 | 进入 Stable 前的条件 |
| --- | --- | --- |
| Core/version/result/句柄值 | Stable 候选 | 固定结果码与句柄语义，补跨版本 Consumer |
| Application 回调生命周期 | Stable 候选 | 拆分或定宽平台枚举，核对描述结构各版本 |
| Asset URI | Stable 候选 | 保持 UTF-8、长度和失败语义 |
| World/Entity | Stable 候选 | 补错误、线程和旧头 Consumer 覆盖 |
| Input/Action | Stable 候选 | 确认常量值、状态结构和映射格式边界 |
| Scene Tree/Transform | Stable 候选 | 固定值结构与层级失败原子性 |
| Scene Instance 运行时读取 | Stable 候选 | 从作者修改接口中明确分组，验证资源租约 |
| Mesh/Texture/Material/Camera | Stable 候选 | 将公开枚举改为定宽常量，补结构布局基线 |
| Type Registry/属性访问 | Experimental | 输出信息结构缺少统一扩展策略，先由样例验证需求 |
| Scene 作者修改/序列化 | Experimental | 0.9.0 新增，需迁移与未知字段策略验证 |
| UI Draw/Debug Draw | Experimental | 面向 Editor/诊断组合，不承诺 1.0 游戏运行时稳定 |
| Granit 平台选择细节 | Experimental | Gneiss 普通 ABI 不泄漏 Granit，后端选择仍可演进 |
| Editor/资产工具与工具 SDK | Experimental | 独立于 Runtime 兼容承诺和安装 target |

## 冻结阻塞项

1. `gneiss_application_platform`、`gneiss_texture_format` 和 `gneiss_texture_color_space` 使用 C
   `enum`；必须改为定宽整数类型加常量，避免不同编译器选择不同底层宽度。
2. `gneiss_field_info`、`gneiss_type_info` 等查询输出结构没有 `struct_size`；若继续公开，应明确固定
   布局或改为可扩展查询结构。
3. Stable 与 Experimental 当前只在文档区分，头文件尚无可机器识别的标记或分组。
4. 当前符号基线记录全部 83 个导出，M-67 需要增加稳定级别清单及自动比较，避免把 Experimental
   误当成冻结 ABI。
5. 现有 Consumer 验证同一提交的安装结果，尚未覆盖旧头文件调用新共享库。
6. 性能和故障诊断还没有发布阈值，不阻塞接口审计，但阻塞最终 1.0.0 发布。

## 验证方式

- 使用 `llvm-readobj --coff-exports` 从 `windows-clang-debug` 的 Shared Library 提取导出符号。
- 按字典序去重后得到 83 个 `gneiss_` 符号，并保存为纯文本基线。
- 检查 `include/gneiss` 下全部 C/C++ 公共头、顶层安装规则和现有 Reference。
- 以 [ADR-023](../decisions/ADR-023-public-api-stability.md) 记录稳定级别和 1.x 兼容边界。

## 后续

M-66 用安装后的公开 SDK 建立代表性样例并收敛 Stable 候选；M-67 处理上述 ABI 阻塞项、为头文件
增加稳定性标记，并把符号、常量和结构布局比较纳入测试。

## M-67 处理结果

M-67 已处理第 1～5 项：审计期间遗漏的 `gneiss_property_kind` 与另外三个公开枚举一并改为定宽
常量；反射查询结构增加 `struct_size`；37 个实验性导出在头文件显式标记；83 个导出建立机器可读
稳定性清单；冻结的 1.0 C 头 Consumer 可直接链接当前共享库。第 6 项归入 M-69。
