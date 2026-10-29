<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-058：普通对象与 Prefab 来源关系统一

## 状态

方案已确认，实施待开展。本次整理只定义目标和迁移步骤，不代表运行时已支持以下模型。
前置 [PLAN-057](PLAN-057-scene-object-storage.md) 已完成重复状态清理，但普通成员和 Prefab 成员仍分开管理。
长期边界见 [ADR-091](../decisions/ADR-091-prefab-object-metadata.md)。

## 当前问题

- `scene_description` 分别保存 `objects` 和 `prefab_instances`，后者重复保存身份、名称、父引用和变换。
- `scene_instance` 管理普通成员，`prefab_runtime_instance` 单独拥有来源节点及清理逻辑，完整遍历需要两路查询。
- Prefab 来源已有根对象，运行时又创建容器根；当前实例变换与来源根变换叠乘。
- `prefab_property_override` 在每个覆盖中重复携带实例身份；普通运行时修改与持久化覆盖需明确区分。

## 目标与非目标

场景对象具有统一身份、层级、组件访问及生命周期；Prefab 表示对象的来源关系。
保留现有场景树和 ECS 分工，不增加 Object 基类、通用 metadata 字典、第二套组件存储或发布事务。
本轮不增加嵌套 Prefab、变体、组件增删覆盖、来源子节点删除／重排覆盖、跨实例对象引用和流送。
普通自有节点可挂在实例根下；对来源内部节点的附加、重排仍保持明确限制，不因统一遍历自动开放。

## 目标描述数据（设计示意）

```cpp
struct prefab_property_override {
  std::string source_node_uuid;
  guid type_guid;
  meta::property_id property_id;
  engine::value value;
};

struct prefab_reference_description {
  std::string uri;
  std::vector<prefab_property_override> overrides;
};

struct object_description {
  std::string uuid;
  std::string name;
  std::optional<std::string> parent_uuid;
  math::transform local_transform;
  std::optional<camera_description> camera;
  std::optional<mesh_renderer_description> mesh_renderer;
  std::optional<prefab_reference_description> prefab;
};

struct scene_description {
  // 格式信息及其他已有来源字段省略。
  std::string uuid;
  std::vector<object_description> objects;
};
```

示意中 `local_transform` 表示组合语义，不要求同时开展无关字段改名。
没有 `prefab` 的对象按现有组件描述创建；带 `prefab` 的对象引用模板，实例根的组件由模板与覆盖产生。
首版拒绝在同一对象同时提供 `prefab` 和独立 camera／mesh_renderer 描述，避免组件值存在两个权威来源。
实例根名称、局部变换以外层对象字段为准；不再同时允许用根节点 Transform 覆盖表达相同值。
文件内使用 `type_guid`、`property_id`，与原先含义模糊的 `type_id`、`field_id` 一起迁移调用方。

## 内存模型与所有权

- 全部实际节点只进入一份场景成员集合，普通创建与 Prefab 创建使用同一入口。
- 场景树继续只持有节点值与 entity ID，不引入资源加载依赖或 Prefab 资源强引用。
- 场景层在实例根节点上逻辑挂接类型化 `prefab_instance_metadata`；物理上可用根节点 ID 索引的私有表保存。
- 该记录保存一份 `prefab_asset_ref` 和来源 UUID → 实际节点 ID 映射；不保存 mesh/material、实体、变换或名称镜像。
- 映射中的节点 ID 是非拥有引用。元数据析构只释放来源引用，不自行销毁节点；场景统一负责删除。
- 实例根也进入来源映射。子节点不各自持有 Prefab 强引用；实例归属查询从映射派生，避免再维护一份可独立修改的归属表。
- 使用来源地址时，实例身份由根对象 UUID 确定。跨实例查询使用“根对象身份 + 来源 UUID”；模板内部 UUID 不能直接作为场景全局唯一键。
- 外层文件 `parent` 首版只能引用文件中的显式对象 UUID，不能用模板内部 UUID 跨实例挂接。

目标类型示意：

```cpp
struct prefab_instance_metadata {
  prefab_asset_ref source;
  std::unordered_map<std::string, scene_node_id> source_nodes;
};
```

这是场景私有关系记录，不是另一种运行时对象或所有者。
最终移除 `prefab_runtime_instance` 的独立节点拥有、创建和销毁职责；能复用的来源解析、差异计算变为操作辅助。

## 属性覆盖与编辑边界

1. 加载来源描述；按来源节点身份验证每项覆盖的组件类型、属性存在性和值类型。
2. 只在本次创建的临时有效描述中应用覆盖，不修改共享源资产。
3. 以外层对象的名称和局部变换配置实例根，再创建普通节点和 ECS 组件。
4. 编辑器修改来源属性时，同时更新实际组件和所属根描述中的覆盖；恢复默认值则删除覆盖并恢复来源值。
5. 普通运行时修改只改实际对象，不自动记覆盖或修改模板。保存 Prefab 关联时使用已记录覆盖，不把模拟期间的变化自动写回。
6. 保存时输出一个带 `prefab` 的根对象，不把它生成的来源子节点再次输出到 `objects`；独立自有节点正常保存。
7. 来源重载时，匹配节点保留运行时身份，未覆盖值采用新来源，覆盖值保留。非法或失去目标的覆盖返回明确诊断，不静默绑定到其他节点。
8. 不引入实例间原子替换保证；保留现有失败语义并针对改动补验收。

未来若支持结构覆盖，应另行设计 added/removed 等明确操作，不能塞入普通属性 value 冒充已有能力。

## 根节点与变换

最终实例对象直接对应来源根，不增加自动容器节点。加载接口返回该普通根节点 ID。
实例根的局部变换替换来源根局部变换；来源子节点保持模板局部变换。
新建实例时，未显式指定变换可由工具复制来源根默认值，但保存后外层字段为显式实例值。
删除整个实例根走统一子树删除，随后移除来源关系；来源子节点结构编辑仍按编辑策略限制。

旧容器模式与新模式不等价：旧世界变换含“容器 × 来源根”。迁移资产必须检查非单位来源根。
旋转叠加非均匀缩放可能产生不能精确表示为 TRS 的剪切，不能简单相乘三个分量或静默丢弃剪切。
仓库样例优先重建到明确的新根变换；必须保留旧视觉的资产可显式保留一个普通父节点，不能把它隐藏为自动容器。

## API 与文件布局

- 统一节点枚举、查询、名称／变换／组件编辑和销毁入口；不要求调用者先判断普通或 Prefab 节点。
- 提供可选来源查询与编辑器覆盖操作；编辑权限、来源信息作为结果或元数据，不单独构造一套节点视图。
- 加载仍由资源加载入口提供 `prefab_description`，实例化消费已经加载的源描述和依赖，不新增 Loader。
- `resource/scene/scene_description.hpp` 保存对象及可选 Prefab 引用；`prefab_property_data.hpp` 保存覆盖键和值。
- `function/scene/scene_instance.*` 保存统一成员和根关联；`scene_object.hpp` 继续复用对象创建。
- 优先在现有文件内收敛；只有实现体量确实需要时再拆私有文件，不预先增加 service。

## 实施顺序

### 第一阶段：统一运行时成员，保持现有格式及变换

- [ ] Scene 统一记录所有普通节点、Prefab 容器和来源节点；原两路枚举调用方全部迁移。
- [ ] 根关联元数据只保存来源及非拥有映射，节点生命周期交给场景；移除双重销毁路径。
- [ ] 编辑器查询来源后执行现有权限限制；统一操作入口不等于允许任意结构编辑。
- [ ] 保持现有容器根、文件 v4 和覆盖表达，验证嵌在普通父节点下的 Prefab 生命周期。

### 第二阶段：统一描述和来源根

- [ ] `object_description` 增加可选 `prefab`，删除单独 `prefab_instance_description` 与顶层实例数组。
- [ ] 删除自动容器根，以外层实例对象身份创建来源根，明确根名称和变换优先级。
- [ ] 覆盖项不再重复携带 instance_uuid；更新编解码、编辑、保存、撤销、重载及 IPC 视图。
- [ ] 场景写入新 schema（拟 v5），同步迁移仓库样例、模板、测试与工具，不保留运行时旧格式兼容分支。
- [ ] 模板内重复 UUID、嵌套引用、非法根覆盖、实例来源缺失等返回具体诊断。
- [ ] 更新当前行为文档与本地验证记录。

第一阶段与第二阶段沿用当前特性分支，不额外发布过渡格式。

## 验收

- 同一模板创建两个实例：来源可共享、节点和组件状态独立，来源寻址不串实例。
- 统一遍历能看到全部实际节点；父子查询及实体数量一致，无额外隐式根（第二阶段）。
- 删除普通父节点会正确删除包含的实例及来源关系；重复删除、失效 ID、跨 World／跨线程访问被拒绝。
- 修改、撤销、恢复来源属性后，实际值与保存覆盖一致；运行时临时变化不会自动成为持久化覆盖。
- 保存并重新加载保持层级、资源关联及覆盖；来源节点不重复展开序列化。
- 重载检验匹配节点身份、未覆盖／覆盖值、来源缺失和失败清理；不泄漏 ECS 实体或资源引用。
- 验证非单位来源根、旋转与非均匀缩放、实例整体变换，防止去掉容器后视觉发生隐式变化。
- 完整本地编译及相关短测试；不运行大场景、GPU 图像验收或耗时远端 CI。
