<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-018：Editor 以版本化工程描述作为启动入口

## 状态

已接受。

## 背景

直接向 Editor 传入资产根和场景 URI 会让启动配置散落在命令行，无法承载工程名称、默认场景及后续
工具设置，也会把“打开一个工程”和“打开一种资产”混为一谈。

## 决策

- Editor 的命令行工程参数只接受 `--project <工程根目录>`；工程描述固定为该目录中的
  `gneiss.project.json`。
- 未传 `--project` 时先运行独立 Project Manager Application；选择成功并销毁其全部资源后，再
  创建正式 Editor Application。
- 工程文件由 Gneiss Editor 解析，描述工程名称、工程内资产根和初始场景。
- Application 仍只接收解析后的资产根，Runtime、场景格式和 Granit 均不理解工程文件。
- 场景修改仍保存到场景作者文件；工程文件只在工程设置变化时单独写入。
- 本机 Editor 状态未来使用工程内独立用户文件，不污染可提交的工程描述。

## 结果

Editor 获得唯一、可版本化且可测试的启动入口，并能在没有命令行参数时交互选择工程。工程语义
保持在工具层，不扩大 Runtime 或 Granit 职责；代价是 Editor 需要独立维护工程格式迁移、路径安全
校验及各平台原生目录选择适配。
