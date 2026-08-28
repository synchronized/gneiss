<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-018：Editor 以版本化工程描述作为启动入口

## 状态

已接受。

## 背景

直接向 Editor 传入资产根和场景 URI 会让启动配置散落在命令行，无法承载工程名称、默认场景及后续
工具设置，也会把“打开一个工程”和“打开一种资产”混为一谈。

## 决策

- Editor 只接受 `--project <目录或 gneiss.project.json>` 作为正式启动入口。
- 工程文件由 Gneiss Editor 解析，描述工程名称、工程内资产根和初始场景。
- Application 仍只接收解析后的资产根，Runtime、场景格式和 Granit 均不理解工程文件。
- 场景修改仍保存到场景作者文件；工程文件只在工程设置变化时单独写入。
- 本机 Editor 状态未来使用工程内独立用户文件，不污染可提交的工程描述。

## 结果

Editor 获得唯一、可版本化且可测试的启动入口。工程语义保持在工具层，不扩大 Runtime 或 Granit
职责；代价是 Editor 需要独立维护工程格式迁移和路径安全校验。
