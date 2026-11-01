<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：准备结果携带来源摘要

2026-10-15，继续实施 [共享资产方案](../plans/asset-scene-ownership.md)。

## 实现与边界

资产准备结果新增 sources：首项为描述文件，纹理另记录负载文件，每项携带原始来源域、
URI、所属资产类型、长度与 SHA-256。摘要直接取自本次读取账本，不增加文件读取或哈希。
来源标记由结果共同持有，因此原 VFS 销毁后仍可比较身份。

这些记录在解码阶段收集，只随整批来源复验成功的结果输出；取消、来源变化或其他失败
仍清空输出。材质依赖继续使用已有 texture_uris，纹理准备配置继续由 texture.profile 保存。
新增数据不持有缓存、RID 或后端对象，不改变公共 SDK。

来源摘要不是完整产物版本。材质自身内容不变时，其纹理依赖仍可能变化；后续版本键必须
组合依赖版本与准备配置。当前加载器仍使用原缓存路径，未实现跨场景共享或固定版本发布。

## 验证

Windows Clang Debug 相关四个测试目标构建成功，定向 CTest 合计 6/6 通过：
render_asset_preparation、texture-load-service、scene-load-application、render_asset_loader、
core-boundary、core-boundary-negative。

新增断言覆盖分块读取的摘要、VFS 销毁后身份存活，以及材质和纹理描述保持不变、仅替换
纹理负载时，描述摘要保持而负载摘要变化。原取消、复验失败、线程池和协作调度测试继续通过。
格式检查通过。asset_preparation.cpp 的 clang-tidy 已执行，修改区域未报告告警；
文件其他区域仍报告复杂度、尾逗号、隐式布尔转换等告警，未将其记录为全文件静态检查通过。

未执行完整 Sponza、Linux、静态库或安装消费者矩阵；未进行远端操作。
