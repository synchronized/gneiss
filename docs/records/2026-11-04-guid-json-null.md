<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# GUID nil 的 JSON 表达调整

2026-11-04，按用户确认，将前一批全零字符串输出改为 JSON null。

- 通用反射 GUID 编解码集中处理 null，不新增 optional 或场景专用读取分支。
- 非 nil 输出 UUID 字符串；nil 输出 null。读取接受 null 和规范 UUID 字符串，全零字符串再保存归一为 null。
- 保留普通 guid parent_uuid；对象/场景身份的非空校验保持。字段缺失仍由 required/默认值规则决定。
- 同步更新导入器、Prefab 提取、示例资产、生成脚本、测试夹具及当前接口文档。
- 新增/更新测试确认 nil 输出 null、null 往返、全零字符串解析、错误 JSON 类型拒绝和对象 UUID null 拒绝。

Windows Clang Debug 全量编译与 9 项相关测试通过，包括反射 JSON、场景/Prefab、导入器及编辑操作。
git diff --check 通过。本轮未运行 MSVC、Linux/GCC、远端 CI 或大场景/GPU 验证。

当前规则见[反射参考](../reference/reflection.md#guid-的-json-表达)与[场景格式](../reference/scene-format.md)。
