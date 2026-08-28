<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 资产索引格式 v1

## 适用范围

`.gneiss/asset-index.json` 是 Editor 和离线工具使用的可重建派生索引。Runtime、公共 C ABI 和
安装 Consumer 不读取该文件。删除索引不会丢失作者数据，可通过工程 `sources/` 重新导入生成。

## 根结构

```json
{
  "format": "gneiss.asset-index",
  "version": 1,
  "entries": []
}
```

根对象只允许以下字段：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `format` | 字符串 | 固定为 `gneiss.asset-index` |
| `version` | 无符号整数 | 当前固定为 `1` |
| `entries` | 数组 | 按规范源路径排序的导入记录 |

## 导入记录

```json
{
  "source": "models/lantern.glb",
  "source_key": "0123456789abcdef",
  "importer": "gneiss.gltf",
  "importer_version": 1,
  "hash": "fnv1a64:0123456789abcdef",
  "state": "ready",
  "outputs": [
    "asset://imported/0123456789abcdef/scenes/scene.scene.json"
  ]
}
```

- `source` 是相对于工程 `sources/` 的规范 UTF-8 路径，不允许绝对路径或 `..` 逃逸。
- `source_key` 标识该源文件独占的 `assets/imported/<source_key>/` 目录。
- `importer` 与 `importer_version` 共同标识生成产物的实现版本。
- `hash` 是变化检测指纹，不承担安全校验；v1 使用 `fnv1a64:<十六进制值>`。
- `state` 可为 `ready`、`stale` 或 `missing`。导入失败不会覆盖上一条完整记录。
- 每个 `outputs` URI 必须位于对应的 `asset://imported/<source_key>/` 命名空间内。

同一索引不得包含重复源路径、重复源键或重复输出 URI。读取器拒绝未知字段和未知版本，避免新旧
工具对字段含义产生不同解释。

## 写入与恢复

索引先写入同目录暂存文件，再替换正式文件；替换失败时恢复旧文件。重建操作只有在所有受支持源
资产导入成功后才替换索引，单个源文件导入失败保留原索引和该源文件的上一份完整产物。
