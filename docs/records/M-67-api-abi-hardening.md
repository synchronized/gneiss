<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-67：公共 API 与 ABI 加固记录

## 结论

M-67 已完成 1.0 候选公共 ABI 的首次加固。当前 83 个 C 导出均有机器可读稳定级别，其中 46 个
Stable、37 个 Experimental；实验性声明使用 `GNEISS_EXPERIMENTAL` 标记，但不改变 ABI。

## 主要改动

- 将 Application 平台、纹理格式、纹理颜色空间和属性类别改为 `uint32_t` 加固定值宏。
- 为 `gneiss_type_info`、`gneiss_field_info` 增加首字段 `struct_size` 和初始化宏。
- 新增 `abi/api-stability.txt`，配置阶段验证其与 83 个导出基线完全一致。
- 新增冻结的 1.0 C ABI 头与 Consumer，验证旧头声明可直接调用当前共享库。
- 为非法零结构大小、定宽类型和返回结构版本补充测试。

冻结测试夹具代表 1.0 候选契约的起点，不追认 0.9.0 为二进制兼容版本。后续 1.x 变更必须保持该
夹具可编译、链接并运行；Experimental 符号不会被误纳入 Stable 兼容承诺。

## 验证

- C11 与 C++20 公共头独立编译。
- 稳定性清单与导出符号基线逐项一致。
- 冻结头 Consumer 覆盖版本查询、Application 创建、运行、退出和销毁。
- Windows Clang Debug 完整构建与测试通过，实际 DLL 导出继续与 83 个符号基线一致。

## 后续

M-68 验证安装包、Shared/Static、纯 C/C++ Consumer 与 Granit package/fetch 路径；M-69 再建立性能、
内存和故障诊断基线。
