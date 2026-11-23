<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# JSON 格式入口拆分验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 本批次

完成 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md) 的入口和格式绑定拆分：

- JSON 实现迁至 core/serialization，删除旧 Meta 序列化入口，迁移源码、测试和构建声明。
- 文本和字节输出分别使用 json::serialize / json::serialize_bytes；增加独立注册表文本重载。
- JSON 回调、反射开关及限制收进单独绑定，注册函数明确区分反射、节点与字节适配。
- 节点专用 JSON codec 也能通过默认资产解码器资格检查，新增回归覆盖。
- 保留现有格式、版本包装、引用和校验语义，二进制资源使用既有显式解码器。

## 验证

- Windows Clang Debug 构建通过。
- 28 项 Meta、JSON、Scene/Prefab、IPC、组件及编辑器回归全部通过。
- 5 项资产管理、材质、资产写入、渲染资产准备/加载回归全部通过。
- 新增文本/字节一致性、独立注册表文本解码、纯节点 codec 资产解码测试。
- 格式检查和 git diff --check 通过。

未执行大型场景或远端 CI。

## 剩余范围

本批次尚未抽取格式无关 reader/writer，字段遍历仍在 JSON 实现中。
未增加通用二进制格式。后续实施由 [DEV-052](../plans/DEV-052-architecture-cleanup.md) 跟踪，
不得将本次目录与接口拆分视为整个序列化后端抽象完成。
