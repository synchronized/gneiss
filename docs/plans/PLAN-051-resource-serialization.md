<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：资源类型与反射序列化

## 状态

已完成初步源码盘点和材质描述命名收敛；材质身份和反射编解码注册已实现并通过编译验收。
Scene/Prefab 已接入统一 manager 与双向 codec，同步和后台描述准备使用同一加载入口；运行验证待统一执行。
材质加载使用 material_description，删除单字段文档包装，所有权保持不变；按最新决定移除材质 v1–v4，仅读写 v5。
材质编解码及 v5 收敛、Scene/Prefab 入口迁移已通过 Windows Clang Debug 全部启用目标编译、格式及边界检查；未执行测试或 CI。

## 目标与非目标

为需要持久化的 CPU 资源描述明确类型身份，并允许通过反射类型查找编解码能力。
复用现有 Registry、asset_ref 和加载入口，不新增资源基类、通用对象容器或版本事务。
不将 GPU 上传、World 实例化、文件系统和缓存放进反射模块。
不要求所有内部临时类型都有持久化 GUID 或注册字段。

## 当前类型清单

| 类型 | 当前数据及职责 | 整理方向 |
| --- | --- | --- |
| material_description | 材质参数、五个纹理 URI，纯 CPU 描述 | 已合并旧 source/document，并接入反射 codec 与通用加载 |
| material_parameters | 参数、颜色数组、枚举和采样数组 | 描述与运行期共用；现有 value 尚不能完整表达 |
| action_map | 直接共享只读输入映射；运行状态由 InputService 管理 | 已移除包装和专用 decoder，注册解码 codec；尚无编码能力 |
| scene_description | 对象、Prefab 实例列表、UUID、源版本和 author_json | 已注册专用读写，保留格式及未知字段能力 |
| prefab_description | 对象、依赖列表、UUID、源版本和 author_json | 已接入统一加载，保留单根约束和依赖校验 |
| mesh_data | 拥有顶点、索引及可选属性数组 | 保留专用二进制 codec，不逐顶点装入 value |
| prepared_texture_data | 解码/准备数据、设备配置、选中变体及 payload_source | 准备结果不是完整持久化描述，不将整个对象序列化 |
| mesh_view | 借用数组视图 | 不作为独立持久化资源 |
| RID、asset_ref、GPU/World 对象 | 运行期身份与所有权 | 不保存地址、控制块、GPU 句柄；依赖在文件中表达为 URI/稳定身份 |

## 现状与问题

asset_manager 已按运行期 type_id 选择显式 asset_decoder 或冻结 Registry codec；VFS 负责读取字节。
反射已有属性访问及可选 codec，value 类别固定；没有数组、枚举、可选字段及嵌套对象 schema。
Scene/Prefab 的现有序列化还保留受支持文档中的未知 JSON 字段，不能直接替换成字段遍历。
运行期注册身份和文件中的格式版本职责不同，不能因新增反射类型就擅自修改文件布局。

## 参考与取舍

本地 CAORS 的 type_builder 将属性与 serialization schema 分段登记，可参考类型关联编解码能力的方式。
Gneiss 先采用固定的可选序列化能力，不移植通用 capability、对象工厂或任意 any 系统。
优势是逐种资源复用现有 codec，改动小、格式兼容；代价是暂时仍保留手写 codec，不能自动序列化任意结构。
逐字段自动序列化以后按实际需求补齐，不能为了资源注册先扩展通用 value 为对象树。

## 建议职责

- type_identity：显式名称与稳定 GUID；runtime_type_of 继续生成进程内 ID。
- Registry：关联类型和可选编解码函数表，不依赖 Resource/Render/World。
- 资源层：实现具体格式、大小上限、字段约束、错误诊断和依赖路径处理。
- asset_manager：读取文件、调度、缓存、持有资源；默认 Loader 可调用已登记的解码器。
- Service：GPU 资源和场景实例创建，不参与反射序列化。

同一类型若有多个格式或版本，须显式选择 codec 或让专用 codec 识别版本；不能仅凭 C++ 类型猜测。
第一版优先使用解码到调用者提供的 T、编码 const T 的有类型函数，由模板生成借用对象适配。
反序列化先构建临时 T，成功校验后替换输出；失败保留输出。输入字节和对象只在调用中借用。
不要求反射持有 asset_ref 或负责构造任意资源。只有解码能力的类型不能伪装成支持保存。

## 实施顺序

1. 完整核对各类读写、缓存键、引用入口及文件版本，确认权威 CPU 数据类型。
2. 已完成材质类型统一为 material_description，删除 material_document 包装；URI 与运行期依赖保持分离。
3. 已实现最小编解码注册接口、材质身份和 v5 保存；旧版本拒绝，示例及夹具迁移。
4. 已统一描述与运行期资源的 material_parameters，移除逐字段转换；默认值及调用点已统一。
5. 已接入显式 decoder 优先、反射 fallback；材质同步加载、重载和后台请求均经同一 manager。
   Render Loader 借用 manager；同步缓存、codec 诊断与描述引用生命周期一并收敛。
   编译用例覆盖选择、失败与冻结；运行验证仍按用户要求延后。
6. 已迁移 ActionMap，保留 1 MiB 上限及加载成功后采用映射的规则；Scene、Prefab 已迁移，同步与后台路径统一；实例创建直接持有准备结果的 Prefab 引用。
   Mesh/Texture 保留专用 codec 和准备流程。
7. 最后按实际编辑需求增加嵌套字段、数组和枚举支持。

## 验收条件

注册类型身份唯一；缺少 codec 返回明确错误；重复或失败登记不留下不完整类型。
有效输入读写往返保持语义；损坏输入、超限及不支持版本失败且不改输出。
材质 URI、默认值、枚举和范围保持；Scene/Prefab 的未知字段及单根约束不退化。
无指针、RID、控制块或设备配置被意外持久化；运行期资源寿命不改变。
实施阶段编译全部启用目标和相关用例，按用户要求暂不执行测试或耗时 CI。

## 待确认项

已确定首版为每类型一组编解码函数、借用输入/输出对象、拥有文本的诊断；文件格式和版本由具体 codec 校验，注册接口不重复传入格式名。
普通描述入口已统一；Mesh/Texture 的专用读取保留。运行测试及大场景验证按用户要求延后。
准备预算与引用采用规则见[资产参考](../reference/assets.md#scene-与-prefab-描述)。
