<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-13 JSON 解析器 Spike 记录

- 日期：2026-08-26
- 环境：Windows、Clang、C++20、Release 优化
- 候选：yyjson 0.12.0、nlohmann/json 3.12.0、simdjson 4.6.4、Glaze 4.6.8
- 结论：选择 yyjson 0.12.0

## 验证样例

最小场景文档包含格式标识、版本、UTF-8 未知字段、超过 IEEE 754 精确整数范围的 `uint64` 值和
浮点数。失败样例包含尾随逗号与非法 UTF-8。

yyjson 与 nlohmann/json 均能解析有效样例并报告语法错误字节位置。yyjson 默认拒绝非法 UTF-8，
通过无异常错误结构返回失败；`uint64` 值 `9007199254740993` 保持精确。

## 本地观察

| 候选 | 最小 C++ 调用层编译 | 错误模型 | 适用性 |
| --- | ---: | --- | --- |
| yyjson 0.12.0 | 约 0.2 秒，另编译一次 C 实现 | 显式错误码、消息和字节位置 | 采用 |
| nlohmann/json 3.12.0 | 约 4.6 秒 | 默认异常；可使用非抛出解析但转换仍需约束 | 不采用 |
| simdjson 4.6.4 | 未进入编译对比 | 显式结果，带输入缓冲和视图生命周期约束 | 当前场景过重 |
| Glaze 4.6.8 | 未进入编译对比 | 强类型模板映射 | Schema 尚未稳定 |

时间是单次本地观察，只用于判断数量级，不作为性能承诺。长期决策见
[ADR-004](../decisions/ADR-004-json-parser.md)。
