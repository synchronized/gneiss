<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 序列化六项收敛验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- JSON 与通用 reader 共用 typed_decode，按 Meta 类别分派，保留对象工厂和自定义解码。
- JSON 编码统一建立 instance，补齐顶层标量、optional 和空分支入口。
- IPC 直接嵌入通用值节点，删除标签拆装和重复结构判断，保留消息与字符串大小约束。
- Mesh JSON 使用 mesh_description 反射字段与 asset_file 版本 3 格式头；资源校验保留
  三角形拓扑、数组数量对应和法线归一化，解码成功后转换为 mesh_data。
- 删除只供 Mesh 使用的 asset_json.hpp；Mesh 二进制布局与解码保持原样。
- 向量、四元数及矩阵共用分量读取，保留固定长度、有限值及 float 范围校验。
- IPC 命令 type_id 使用 guid::string/parse，不再维护 32 字符十六进制编解码。

规则见[序列化参考](../reference/serialization.md)、[Value 参考](../reference/value.md)
与 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证结果

- windows-clang-debug 构建通过；首次构建发现模板头未加入宿主可见清单，补齐后重新构建通过。
- 56 项相关回归全部通过，包含 Meta、JSON 与内存后端、Scene/Prefab、Mesh、资产加载、IPC、
  编辑器、核心边界及内部头可见性。
- 新增回归覆盖 JSON 顶层标量/optional/空值、通用后端 optional、数学值、Mesh 非法数组及
  法线、IPC 标准 GUID 字符串；既有非默认构造对象、自定义 codec 与生命周期用例通过。
- 修改源码的 clang-format 检查及 git diff --check 通过。

未运行大型场景或远端 CI，未执行远端操作。
IPC 命令 GUID 改用标准带连字符字符串，旧 32 字符编码不兼容，通信两端须同步更新。
Mesh JSON 仍为版本 3；字段读取统一后部分非法输入的诊断文本或报错顺序可能改变。
