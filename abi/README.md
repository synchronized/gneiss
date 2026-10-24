<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 接口边界检查

完整 C SDK 与动态 Game Module 均已撤销，当前没有公共 C 操作函数。
原模块符号基线与稳定级别清单已删除，`check_public_api.cmake` 拒绝恢复 C 导出。

`cpp-type-inventory.json` 只登记暂留的符号可见性宏与结果码常量；它不代表公共对象 SDK。
历史 ABI 证据保留在 docs/records，不作为当前兼容承诺。
