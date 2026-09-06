<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-220～M-228：0.34.0 离线纹理构建验收记录

## 结果

Gneiss 0.34.0 已将 Granit 基线升级至 0.17.0，并完成 PNG 作者纹理到 RGBA8 KTX2 运行资产的确定性
Cook。Texture 描述中的颜色空间参与缓存键，JSON 资产 URI 与构建元数据使用派生 `.ktx2` 路径；
Runtime 校验容器和完整 Mip 链后，由 Granit 创建对应 Mip 数并逐级上传。

KTX-Software 经评估未进入本版本依赖。当前无压缩 RGBA8 范围由轻量 Writer/Reader 覆盖；等 Basis
或块压缩形成真实需求时再引入私有工具依赖，避免提前承担大型工具链和许可面的维护成本。

## 本地验证

- Windows MSVC 与 Clang Shared Debug：严格警告全量构建及完整测试均为 136/136 通过。
- KTX2 测试覆盖 sRGB/Linear、奇数尺寸完整 Mip、逐字节确定性、截断、未知格式、超级压缩和运行时
  多 Mip 加载；工程发布测试确认包内存在 KTX2、移除 PNG，并重写 Texture URI。
- Lantern Gallery 的 18 个运行资产首次全部构建，第二次 18/18 命中缓存且两份输出逐字节一致；4 张
  PNG 转为 KTX2，其中 512×512 纹理包含 10 个 Mip。
- 使用 Cook 后 Lantern Gallery 资产启动 `gneiss_runtime --smoke`，场景、游戏模块与关闭流程成功。
- `git diff --check` 通过。

## 远端验证

- [Linux Actions 34048007253](https://github.com/synchronized/gneiss/actions/runs/34048007253)：
  Clang/GCC、Shared/Static、Granit 无头运行及 Sanitizer 全部通过。
- [Windows Actions 34048010183](https://github.com/synchronized/gneiss/actions/runs/34048010183)：
  MSVC Shared/Static 与安装后 Consumer 全部通过。

两套矩阵均验证发布候选提交 `60d2b75`。本节仅回填验收结果，不重复运行矩阵。
