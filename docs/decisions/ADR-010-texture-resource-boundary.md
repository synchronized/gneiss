<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-010：纹理资源与后端镜像边界

- 状态：已接受
- 日期：2026-08-27

## 背景

Gneiss `0.3.0` 已能加载 Mesh、固定颜色 Material 并完成交互渲染，但尚无纹理资源、UV 或图片解码。
纹理既要被场景资产和未来工具引用，又不能让 Granit 句柄、GPU 格式或图片解码库进入公共接口。

## 决策

- Texture 是 Render Service 管理的独立 64 位 RID；Material 只保存 Texture RID，不拥有后端对象。
- 公共创建接口接收已解码的二维像素、尺寸、行跨度、像素格式和颜色空间，调用期间复制数据。
- 首个公共像素格式只承诺 RGBA8；颜色空间显式区分线性与 sRGB，零尺寸、溢出和非法行跨度失败。
- 图片文件、解码器和 Texture 源资产格式属于 VFS Loader 内部实现，不进入 C ABI。
- Render Service 保存后端无关的 CPU 资源；Granit Render Service 按需创建并缓存 GPU Texture、View
  和 Sampler 镜像，资源销毁或后端重建时使对应镜像失效。
- Material v2 通过逻辑 URI 获取 Texture 租约；缓存租约保证 Material 有效期间 Texture RID 存活。
- 首版只支持二维单层纹理和一个 base-color 槽位；Mip、压缩纹理、数组、Cube、Render Target、
  流式上传与 bindless 资源后续按真实用例扩展。

## 影响

公共资源契约可以独立测试且不绑定 Granit；图片解码方案可替换，GPU 生命周期也与场景持久化隔离。
代价是首版保留 CPU 像素副本，并需要显式维护 Material 租约、GPU 镜像和销毁顺序。

## 替代方案

- 公共 API 直接接收 PNG/JPEG：接口简单，但把文件格式和解码策略冻结为运行时 ABI。
- Texture 只存在于 Granit 后端：无法由无后端测试、资源缓存和未来编辑器稳定引用。
- 将 Texture 嵌入 Material：减少一种 RID，但重复资源、共享和生命周期语义会变得含糊。
