<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 字段引用策略覆盖

## 已实现

field_policy 增加 save 与可选 reference。JSON 和快照统一选择字段覆盖、目标类型默认、
框架默认引用策略；显式空策略可以覆盖类型的外部引用或多态策略。
只覆盖直接引用字段，拒绝将引用覆盖配置到非引用字段。字段策略不传播给容器元素。
save=false 在文件与快照中均排除字段；恢复不要求该字段，文件读取不修改该字段。

快照恢复检查外部身份与内嵌内容的策略一致性，非多态字段拒绝派生内容。
外部身份捕获回调接收按 C++ 继承转换后的声明类型地址，避免多继承地址偏移错误。

## 验证

反射 JSON 和序列化定向测试通过，覆盖：

- 字段覆盖类型默认的多态与非多态表示，两种方向均验证。
- 外部身份与内嵌内容互相覆盖，JSON 往返与快照内容类型。
- 空引用、save=false、快照恢复独立性以及缺少外部解析器明确失败。
- 不保存却必填、非引用字段配置引用策略的错误路径。

windows-clang-debug 全目标构建通过，45 项相关 CPU 回归全部通过。
日志为 build/plan108-field-reference-full-build.log 与 build/plan108-field-reference-tests.log。
格式检查与 git diff --check 通过。未运行 GPU、大场景、远端 CI。
