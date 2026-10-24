<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：统一拥有数据的引擎值

状态：实现完成，编译与静态检查通过；运行回归延期。

目标：以 Core 的 value/value_kind 取代两套 property_value/property_kind；保留固定值类别，
不引入任意对象、动态容器或通用 any 框架，不移植 CAORS 注册器。

1. 新建 core/value.hpp，持有字符串，不依赖反射注册表。
2. 迁移反射、组件、Editor、Runtime 和 Prefab；合并 World 重载、删除字符串借用转换。
3. getter 输出 value&，setter 借用 const value&；处理字符串复制产生的分配失败。
4. 保持资产和 IPC 协议，更新所有权文档，编译值类型与相关用例；不执行测试、大场景或 CI。

## 验收状态

Windows Clang Debug 全部启用目标编译通过，新增 value 独立头使用、字符串所有权、复制/移动和类别
对应关系用例；反射回调分配失败、输出保持和字符串快照用例已迁移并编译。
Core 边界、公共接口、修改文件格式及 diff 空白检查通过。旧 property_value/property_kind 类型已移除，
资产和 IPC 的专用协议载荷保留。按要求未执行测试、大场景或 CI。
当前契约见 [引擎通用值](../reference/value.md)。
