<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 接口边界检查

完整 C SDK、动态 Game Module 与公共 include 目录均已撤销，当前没有公共 C 操作函数或 SDK 头。
旧符号基线、稳定级别及类型映射清单均已删除。
cmake/check_public_api.cmake 检查公共接口残留；以后新增接口时应按实际需求更新设计与检查。
历史 ABI 证据保留在 docs/records，不代表当前兼容承诺。
