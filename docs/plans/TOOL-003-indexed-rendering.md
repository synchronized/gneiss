<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-003：索引渲染最小闭环

## 状态

- 状态：已完成
- 目标：让 Mesh Binary v1 的唯一顶点和索引贯通 Resource Service 与 Granit 绘制路径。

## 背景与目标

TOOL-002 已减少 Runtime 文件体积和解析时间，但 Loader 仍按索引展开顶点。本任务保留索引直到
后端提交，并覆盖公共 ABI 兼容、生命周期和错误边界。

## 非目标

- 静态 GPU Mesh 缓存、异步上传或显存驱逐。
- UInt16、自适应索引宽度、Meshlet 或间接绘制。
- 修改 Mesh Binary v1。

## 已确认决策

- 公共索引固定为 UInt32；项目未发布，不保留内部开发阶段的旧描述布局。
- 无索引描述保持合法，由 Granit 提交路径生成顺序索引。
- 资产格式属于 Gneiss，Granit 只消费后端无关资源数据。

## 实施顺序

1. 建立 ADR，冻结所有权、兼容与后端边界。已完成。
2. 扩展 C ABI 与 Resource Service，验证并复制索引。已完成。
3. Binary Loader 直接提交唯一顶点和索引。已完成。
4. Granit 创建 Index Buffer 并执行 Indexed Draw。已完成。
5. 补齐测试、文档和 Lantern 验证。已完成。

## 测试与验收

- C11/C++20 公共头和安装后消费者继续通过。
- 覆盖索引成功创建、越界、数量错误、保留字段与错误结构尺寸。
- 二进制 Loader 保留 4 个唯一顶点与 6 个索引，不展开为 6 个顶点。
- Granit Smoke 与 Lantern 示例可见结果保持正常。
