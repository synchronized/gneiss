<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 发布包清单格式 v1

`gneiss_project package` 在可运行目录根生成 `gneiss.package.json`。清单用于确认交付物的配置、入口和
内容完整性，不替代工程描述，也不提供签名或来源认证。

```json
{
  "format": "gneiss.package",
  "version": 1,
  "gneiss_version": "0.34.0",
  "project": "My Game",
  "profile": "development",
  "platform": "windows",
  "architecture": "x86_64",
  "entrypoint": "run.cmd",
  "files": [
    {
      "path": "bin/gneiss_runtime.exe",
      "size": 123456,
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ]
}
```

## 字段与规范化

- `format` 固定为 `gneiss.package`，`version` 当前固定为 `1`。
- `profile` 是 `debug`、`development` 或 `shipping`；`platform` 与 `architecture` 描述本机目标。
- `entrypoint` 是包根内平台启动脚本，Windows 为 `run.cmd`，其他当前支持的平台为 `run.sh`。
- `files` 不包含清单自身，按 UTF-8 相对路径升序排列；路径使用正斜杠且不得逃逸包根。
- `size` 是字节数，`sha256` 是 64 个小写十六进制字符。

`gneiss_project verify <发布包目录>` 会重新枚举目录，并逐项比较路径、大小和 SHA-256。文件被修改、
缺失或出现清单外文件时返回失败。该校验只能发现内容变化，不能证明发布者身份；正式签名属于后续
发布基础设施。

## ZIP 封装

传递 `--zip` 时会在目录包旁生成同名 `.zip`。归档使用稳定路径顺序、固定 DOS 时间、UTF-8 文件名
和 Store 模式；在文件内容、工具版本、平台及配置相同时，归档字节保持一致。目录包仍是权威内容，
ZIP 不使用第二套收集规则。

## 纹理运行资产

0.34.0 起，目录包构建会把作者 PNG 转换为 RGBA8 KTX2，并将 JSON 中对应的 `asset://` URI 重写为
`.ktx2`。包内不保留被转换的 PNG；`.gneiss-build.json` 记录的输出路径与依赖同样使用派生 URI。
Debug、Development 与 Shipping 使用相同 Cook 规则，Shipping 仍只保留入口资产可达的传递闭包。
