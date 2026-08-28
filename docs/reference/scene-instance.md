<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景加载、实例、序列化与卸载

## 加载阶段

`gneiss_scene_instance_load` 接收所属 Application 和规范场景 URI，在 Application 创建线程同步执行：

1. 通过 VFS 读取并完整校验场景描述。
2. 获取场景引用的全部 Mesh 与 Material 缓存租约。
3. 父节点优先创建 Entity 和 Scene Node，再设置 Transform 与组件。
4. 全部成功后才发布非零场景实例句柄。

解析或资产阶段不会修改 World。提交阶段任一步失败都会逆序销毁本次创建的实体、节点和资产引用，
因此失败前后 World 实体数量与资源存活状态一致。当前加载只允许 Application 创建线程调用。

## 所有权与卸载

场景实例由所属 Application 独占，不能跨 Application 使用。`gneiss_scene_instance_unload` 依次释放
实体、节点和资产租约，句柄立即失效；重复卸载返回句柄错误。Application 销毁时会先卸载仍存活的
场景，再销毁 World 和 Render Resource Service。

C++ `gneiss::scene_instance` 提供移动专属的 RAII 包装，必须在所属 `gneiss::application` 之前析构。
场景中的 Mesh Renderer 只借用 RID，资源生命周期由场景实例持有的租约保证。

## UUID 查询

`gneiss_scene_instance_find_node` 使用场景文件中的规范对象 UUID 查找借用 Scene Node ID。找不到返回
`GNEISS_ERROR_NOT_FOUND`。节点 ID 只在场景实例未卸载且 World 存活期间有效，可用于运行时更新
Transform；持久化文件仍不得保存节点 ID。

## 节点枚举

`gneiss_scene_instance_get_node_count` 与 `gneiss_scene_instance_get_node_info` 按作者场景 `objects`
数组的稳定顺序枚举实例节点。节点描述包含当前 Node ID、父 Node ID、Entity ID、规范 UUID、可选
显示名称、当前局部 Transform、组件标志、Camera 作者值以及可选 Mesh/Material 作者 URI。作者
JSON 可为对象增加可选字符串 `name`；旧场景无需迁移，名称为空时由调用方选择 UUID 等回退显示
文本。V1/V2 调用方仍可使用旧结构尺寸读取原有字段；V3 字段只在调用方提供完整结构尺寸时写入。

节点描述中的 UTF-8 UUID 和名称是实例借出的“指针 + 长度”视图，不以零结尾为契约，不能由调用方
释放；实例卸载或 Application 销毁后立即失效。`out_info` 必须以
`GNEISS_SCENE_INSTANCE_NODE_INFO_INIT` 初始化。索引越界返回 `GNEISS_ERROR_NOT_FOUND`；节点或实体
已被外部销毁时返回句柄错误。C++ 包装提供对应的 `get_node_count` 和 `get_node_info`。

## 作者节点编辑

`gneiss_scene_instance_create_node` 原子创建不含可选组件的通用作者节点。调用方提供稳定 UUID、可选
名称、实例内父节点和局部 Transform；创建失败不会留下 Entity、Scene Node 或作者对象。

`gneiss_scene_instance_set_node_name` 同步修改运行时显示名称和作者名称；空名称表示清除。
`gneiss_scene_instance_reparent_node` 同步修改 Scene Tree 和作者父 UUID，支持挂到根，拒绝自身、循环
关系和实例外父节点。两项操作均先准备可能分配的作者数据，再修改运行时状态，失败时保持原状态。

`gneiss_scene_instance_create_mesh_renderer_node` 原子创建带 Mesh Renderer 的作者节点。调用方提供
稳定 UUID、可选名称、实例内父节点及 Mesh/Material URI；实现会先获取两个资产租约，再创建 Entity、
Scene Node 和 ECS 组件。任何阶段失败都不增加作者对象或泄漏资源。

`gneiss_scene_instance_set_mesh_renderer` 原子替换实例节点的 Mesh 与 Material。新资产全部获取成功且
ECS 组件更新成功后，才替换作者 URI 和旧租约；失败保留原引用。父节点必须属于同一场景实例，节点
ID 和实例不能跨 Application 使用。这两项操作仅限 Application 创建线程。

`gneiss_scene_instance_destroy_node` 删除没有子节点的作者节点，并使对应 Entity、Scene Node ID 与
资产租约失效。有子节点时返回 `GNEISS_ERROR_INVALID_STATE`，未知节点返回句柄错误。调用方如需删除
子树，必须先按子节点到父节点的顺序显式删除；接口不会隐式改变其他作者对象。

C++ `gneiss::scene_instance` 提供对应强类型包装。创建、重命名、重挂接、替换与删除会进入后续
序列化结果，但不会自行写入来源文件；Editor 仍负责脏状态和原子保存。

## 运行时属性序列化

`gneiss_scene_instance_serialize` 将实例当前的局部 Transform、Camera、已创建作者节点和 Mesh
Renderer 引用合并回加载时的作者文档，并输出当前版本的 UTF-8 场景 JSON。保存会保留未被 Runtime
解释的未知字段，不会覆盖来源文件，也不会持久化 Entity ID、Scene Node ID 或资源 RID。

C 接口使用两次调用：先以空 `buffer` 和零 `capacity` 查询不含字符串终止符的字节数，再提供足够
容量的缓冲区。容量不足返回 `GNEISS_ERROR_INVALID_ARGUMENT`，同时通过 `out_length` 返回所需长度。
C++ `gneiss::scene_instance::serialize` 直接输出到调用方提供的 `std::string`。

序列化只允许在所属 Application 创建线程执行。实例中的实体、节点或原有 Camera 已被调用方删除
时返回对应错误且不产生部分输出。输出文本如何落盘由工具或宿主负责；只读 VFS 不会被保存操作
隐式修改。
