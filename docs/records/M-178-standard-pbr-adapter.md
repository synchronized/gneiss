<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-178：标准 PBR 资产适配

## 结果

Granit Render Service 已改用 Granit 0.10 Render Pipeline、Mesh 与 Material Instance，移除原有
手工 Pipeline 执行路径。Gneiss 作者资产仍保持后端无关，运行时将 Mesh、Material RID 和 Scene
Snapshot 投影为仅由渲染线程持有的 Granit 对象。

Material v3 增加基础颜色、可选基础颜色纹理、金属度和感知粗糙度。glTF 导入器保留对应因子；旧
Material v1/v2 继续按非金属、全粗糙默认值加载。Granit 标准 PBR Shader 与 Material 归档以构建时
嵌入方式提供，不要求最终用户部署 Granit 源资产目录。

## 生命周期与回退

- 纹理、Mesh 或 Material RID 的资源版本变化会使对应后端投影失效并在下一帧重建。
- Material Instance 借用 Texture View；任何纹理投影失效时，同步清空材质投影，避免悬空借用。
- 缐少法线的旧 Mesh 使用稳定的向上默认法线；缺少纹理时使用内建白色、法线和线性默认纹理。
- Shader 或 Material 创建失败不会发布半初始化对象，错误沿渲染完成回执返回。

## 验证

- Windows Clang Shared Debug：完整构建及 132/132 测试通过。
- glTF 导入与 Material Loader 测试覆盖金属度、粗糙度及旧格式默认值。
- Runtime、Editor、Lantern Gallery、PBR Resolver 与 Granit 平台冒烟通过。

## 已知边界

- 本阶段只映射基础颜色、金属度和粗糙度；法线、遮蔽、自发光纹理留待实际资产用例驱动。
- 环境光照及细分 GPU 阶段诊断分别由 M-179、M-180 完成。
