<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-131 Prefab 作者文档与原子保存实施记录

## 结论

M-131 复用现有内部 `prefab_description` 作为 Prefab 作者文档，并补充保留未知字段的确定性序列化，
没有建立第二套 Schema。Editor 新增 Native FileSystem 多文档事务：先持久化清单、基线镜像和目标
镜像，再逐项替换文件，最后写入提交标记。Editor 启动时会在创建应用前恢复未完成事务。

该能力仅为 Editor 内部实现，不构成公共 API、稳定资产 ABI 或通用 VFS 写接口。

## 已验证行为

- Prefab 解析、修改和重新序列化保留未知根字段与对象字段，并复用场景 v4 的对象序列化规则。
- 提交前对所有文件执行完整基线内容检查；任一文件被外部修改时不写入任何目标。
- 事务数据位于资产根目录的 `.gneiss/transactions`，目标只接受资产根内的规范化相对路径。
- 未写入提交标记的中断事务在恢复时回滚到全部基线；已写入提交标记的事务保留新值并清理日志。
- 新建文件以“不存在”作为基线；路径穿越、绝对路径、重复目标和缺失父目录均被拒绝。
- Editor 无参数启动和直接打开工程都会在进入事件循环前执行恢复，恢复失败会进入现有启动诊断。

## 验证

- Windows Clang Shared Debug：Prefab 描述、作者事务、Editor 启动与项目管理专项测试 4/4 通过。
- Windows Clang Static Debug：对应目标构建通过，专项测试 4/4 通过。
- 新增故障注入覆盖首次替换后中断、提交标记后中断、来源冲突、新建文件和越界路径。
- 相关新增及修改源码通过 `clang-tidy`，提交前差异检查通过。

Create Prefab、Apply 和 Unpack 将在 M-132 至 M-134 中复用该事务入口完成具体作者命令。
