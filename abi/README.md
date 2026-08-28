<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ABI 基线

本目录保存 Gneiss Stable C ABI 的可机器比较基线。`baseline/gneiss-1.0.0-c.txt` 当前记录 1.0.0
开发起点的全部 C 导出符号，用于发现意外删除；在发布候选冻结 Stable 清单前，它不代表兼容承诺。

基线文件按符号名升序排列，一行一个符号。自动比较、结构布局和常量值验证由 M-67 接入。
