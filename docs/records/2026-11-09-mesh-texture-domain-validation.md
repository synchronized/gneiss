<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 网格与纹理描述的领域校验

## 变更

移除 Mesh/Texture 描述在通用反射序列化中的 validation 注册，领域校验实现分别放在
mesh_description.cpp 与 texture_description.cpp。asset_file 的对应格式接入校验回调，
保存前和解码独立候选后执行，保留错误字段路径。

网格在转换为 mesh_data 前仍检查拓扑、顶点数量、UV/法线数量和法线归一化。
纹理检查非空来源、已知用途与颜色空间，以及法线纹理要求 linear 的约束。
直接 decode_texture_payload 也先执行描述校验，不依赖调用方曾经过 JSON 文件入口。
普通 JSON 可以承载尚未满足业务约束的描述；required 与枚举表示等结构策略不变。
材质参数、采样策略、局部组件和数学值的校验迁移尚未完成。

## 验证

定向构建与以下 3 项 CPU 测试通过：render_asset_preparation、render_asset_loader、
texture-load-service。新增通用内容与文件边界分离测试，并检查原生纹理负载入口的错误路径。
日志：build/plan108-mesh-texture-domain-build.log、build/plan108-mesh-texture-loader-build.log、
build/plan108-mesh-texture-domain-tests.log。
未执行全量构建、GPU、大场景或远端 CI。
