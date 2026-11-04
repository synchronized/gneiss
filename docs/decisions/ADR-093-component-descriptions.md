<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# ADR-093：场景与 Prefab 使用类型化组件描述列表

## 状态

已接受，实施见 [PLAN-060](../plans/PLAN-060-component-descriptions.md)。

## 决策与原因

object_description 用统一列表取代 camera/mesh_renderer 固定可选字段。
每条描述携带稳定 GUID 与自有不可变 C++ 数据，复制共享只读内容，修改替换整条描述。
运行期类型 ID 用于安全 get<T>，不写入文件。文件采用 `$type_guid` 与 `$content` 外壳。
Meta 决定具体解码类型，场景功能层负责描述到 World 组件的转换及资源绑定。
这允许新增描述 codec 而不扩充场景解析器，不引入公共虚基类或庞大的通用对象系统。
代价是描述对象需单独分配；当前不做小对象优化。

## 限制

每种组件描述每对象最多一条；同类型多组件需另行设计稳定实例身份。
文件未知类型、重复 GUID、坏内容均报错。运行时不支持的已知描述也必须报错。
Prefab 覆盖暂保留已支持的 Transform、Camera 属性桥接，不将描述 GUID 冒充 ECS 类型 GUID。
当前格式未发布，统一迁移已有数据，不引入兼容分支。

组件外壳扩展字段按 GUID 保留，`$content` 以 codec 完整输出为准；不合并旧内容，避免删除字段复活。
