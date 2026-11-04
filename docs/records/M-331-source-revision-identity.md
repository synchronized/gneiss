<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：来源复验身份传递与摘要查询

2026-10-15，继续接入 [共享资产方案](../plans/asset-scene-ownership.md)。未完成版本键生成或场景共享。

## 实现

内部 file_system 增加可选 identify_source，透明读取包装可委托原始来源身份；普通后端返回
UNSUPPORTED 时，VFS 继续使用挂载身份。其他错误直接传递，不以包装身份掩盖原来源缺失。
委托成功后 VFS 检查来源非空、类型一致及 URI 合法。未涉及公共 C ABI 或安装 SDK。

source_revision_file_system 将身份查询交给所持有的原 VFS。因此现有场景加载所建立的根挂载
包装，在进行身份查询时不会丢失原始根挂载或覆盖挂载身份，多层包装同样适用。查询不读取文件。

新增 observed_revision，读取账本已记录的长度和 SHA-256；未建账返回 NOT_READY，非法路径或
错误清空输出。读取受账本锁保护，不执行 I/O。该摘要只表示已经观察到的内容，不能证明文件
当前未改变，不能取代最终 verify，也不包含资产依赖版本与设备准备配置。

## 验证

Windows Clang Debug 构建通过；定向 CTest 6/6 通过：resource_service、scene-load-application、
render_asset_loader、source-revision、core-boundary、core-boundary-negative。
新增测试覆盖根与覆盖挂载的双层包装身份保持、身份查询不建账、未建账查询、读后摘要匹配、
源文件变化后旧摘要仍可观察但 verify 拒绝、非法路径输出清理，以及缺失来源不回退。
所改 C++ 格式检查、两份修改实现的 clang-tidy、文档相对链接及 git diff --check 通过。

## 剩余工作

缓存键仍需接入来源内容、依赖版本和准备配置，场景加载器尚未使用固定版本缓存获取。
共享任务订阅、取消隔离、独立候选 RID 和实例引用切换尚未实现，不将上述查询能力描述为
端到端共享已完成。未执行完整 Sponza、Linux/静态/消费者矩阵，未触发远端操作。
