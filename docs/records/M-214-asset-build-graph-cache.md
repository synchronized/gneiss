<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-214：确定性资产构建图与缓存实施记录

## 结果

新增内部 `asset_build` 模块，通过注册表按最长文件后缀选择处理器。首批注册 Texture、Mesh、Material、
JSON 文档和普通文件处理器；同一标识或后缀不能重复注册，未知文件由普通文件处理器承接。

构建器扫描 JSON 中的 `asset://` 引用并形成传递依赖图。缓存键覆盖文件内容、处理器标识与版本、目标
平台、架构、构建配置及依赖缓存键，不依赖文件时间戳。缓存位于工程构建目录，缓存文件和最终输出均
先写入唯一暂存路径再提交，不覆盖已有输出。

Development 构建全部资产；Shipping 从调用方提供的根 URI 遍历可达集，并统计裁剪数量。当前首批
处理器保持原有运行资产字节，后续里程碑在同一处理器边界加入格式转换。

## 验证

- 首次构建生成四项产物，第二次构建四项全部命中缓存。
- 修改 Texture 后，Texture、依赖它的 Material 及上层 Scene 缓存键均失效，未关联资产继续命中。
- Shipping 从 Scene 根正确保留三项可达资产并裁剪一项未引用 Texture。
- 缺失根资产、循环或缺失依赖、重复处理器及既有输出均被拒绝。
- Windows MSVC Debug 目标构建和 `gneiss.tooling.asset_build` 测试通过。

