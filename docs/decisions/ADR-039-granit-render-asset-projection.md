<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-039：Granit 渲染资产只作为后端投影

- 状态：已接受
- 日期：2026-09-06

## 背景

Gneiss 当前拥有 Mesh、Material、Texture 的 RID、作者 JSON、Runtime Mesh Binary、VFS 缓存和事务式
热重载；Granit Render Service 再用内嵌 Shader 和显式 GPU 对象构造后端镜像。Granit 0.10 已提供
标准 PBR Shader 资产、Material、参考 Render Pipeline 与 Environment Map，但这些能力使用 Granit
自己的 `.grshader`、`.grmat` 和 `.grenv` 数据以及 Renderer 句柄。

若直接让 Scene、公共 API 或作者资产引用这些格式，会把渲染后端版本和 Binding 契约扩散到引擎
逻辑层；若继续维护 Gneiss 私有 PBR Shader，又会与 Granit 的参考管线形成重复实现。

## 决策

- Gneiss 继续拥有源资产、作者格式、Import IR、逻辑 URI、资源 RID、依赖关系、修订和热重载事务。
  Material 与 Environment 只表达后端无关的渲染语义，不保存 Granit 句柄或资产路径。
- Granit 标准 PBR Shader、Material 实例、Mesh、Environment Map 与 Render Pipeline 都属于
  `granit_render_service` 的私有 GPU 投影，只能由渲染线程创建、更新和销毁。
- 接入标准 PBR 时迁移到 Granit 参考 Render Pipeline 的公共 Mesh、Material、Scene 和 Draw Binding
  契约，不从其示例私有 `gpu_scene`、执行器、加载器或 UI 复制实现。
- Granit 安装的标准 PBR Shader Library `.grshlib` 与 `.grmat` 是后端构建输入。Gneiss 通过 Granit 公共元数据、
  Schema 和目录契约验证并嵌入它们，同时将运行时依赖暂存到自己的安装数据目录；Runtime 不依赖
  开发机绝对路径。
- Granit 专用产物只允许存在于 Gneiss 派生缓存或安装数据中。缓存键至少包含 Gneiss 资产修订、
  Granit 版本、Renderer backend/profile 和输入内容摘要；不匹配时重新生成，不能迁移为作者数据。
- Shader Library 的归档由后端投影层持有，GPU Shader 变体与内容 ID 解析由 Granit 公共 Library
  接口负责。先销毁 Material 与 Pipeline，再销毁 Library，最后释放归档字节。
- Environment 作者数据保存环境源 URI、强度、水平旋转和必要的导入设置；导入器产生的 `GRENV`
  字节属于派生后端产物。Render Service 从 VFS 取得字节后创建候选 Environment Map，成功才原子
  替换旧投影，失败继续使用旧环境或内建中性环境。
- PBR Material 与 Environment 更新复用现有不可丢弃渲染 Command、修订检查、进度回执和失败
  原子性；大资源正文不得进入普通 Frame Packet。
- 公共 C/C++ API、Scene/Prefab 作者格式和 IPC 只暴露 Gneiss 类型、RID、URI 与语义值，不暴露
  Granit 类型、稳定内容 ID、Binding 编号或专用文件格式。

## 最小适配边界

| 层 | 输入 | 输出/所有权 |
| --- | --- | --- |
| 作者与导入 | glTF、图片、环境源及 Gneiss JSON | 后端无关 IR、Gneiss URI 与派生资产 |
| Runtime 资源 | PBR 参数、Texture/Environment RID 与修订 | 不可变 Gneiss 资源快照 |
| Granit 投影 | 资源快照、暂存标准资产和派生字节 | 渲染线程独占的 Granit 对象 |
| Frame Packet | 相机、动态 Transform、可见性和资源快照引用 | 提交时不可变的逐帧状态 |

首个 PBR 映射只包含基础颜色、金属度、粗糙度和现有 Base Color Texture；其余 glTF 槽位在资产
模型具备真实用例后追加。首个 Environment 映射只包含一个活动环境、强度与水平旋转。

## 影响

- Gneiss 可以复用 Granit 的标准 PBR 与环境实现，同时保留后端替换、无后端测试和作者格式演进能力。
- Granit 升级导致的专用格式变化只会使派生缓存失效，不需要迁移 Scene、Prefab 或项目源资产。
- PACKAGE、FETCH 和父工程目标三种 Provider 必须得到相同的标准资产集合，安装测试也要验证数据文件。
- 从当前显式 Graphics Pipeline 迁移到参考 Render Pipeline 是一次后端投影替换，需要同时校验顶点
  布局、材质参数、UI/Debug Draw 合成、热重载和线程所有权，不能只更换 Shader 文件。

## 替代方案

- **Scene 直接引用 `.grmat`/`.grenv`**：接入直接，但冻结 Granit 格式并破坏后端隔离。
- **永久保留 Gneiss 私有 PBR Shader**：短期改动少，但重复维护 BRDF、Binding 和跨后端变体。
- **把 Granit Material 句柄存入 ECS 或公共 RID 表**：减少一层映射，但让逻辑层持有 GPU 后端状态。
- **运行时从 Granit 源码或安装绝对路径读取资产**：开发方便，但安装、打包和父工程消费不可重复。
- **复制 Model Viewer 的资产与加载代码**：能快速显示结果，但依赖示例私有契约并形成第三套资产状态。
