# W-2 prototype: 統合startupと等価性検証ハーネス

状態: proof of concept(2026-08-19)。リリース対象ではありません。
関連: [R-01](../../docs/research/startup-files.ja.md)、[実験0002](../../docs/experiments/0002-unified-startup-poc.ja.md)、[環境整備計画](../../docs/infrastructure.ja.md) W-2

## 目的

「共通crt + family別vector include」方式の統合startup(`crt0_ch32.S`)が、WCH EVTのstartupと等価なELFを生成することを、実機なしで機械検証する。

## 重要な制約

**vendor由来のvector table(`vectors_*.inc`)はこのリポジトリへコミットしない**(EVT取込条件はQ-030/Q-031で未決定)。`extract_vectors.py`がローカルのEVTミラーからその場で生成する。将来は`ch32-device-data`からの生成へ置き換える。

## 構成

| ファイル | 内容 |
|---|---|
| `crt0_ch32.S` | 統合startup。vector includeとCSR初期値を`-D`で注入する(own実装) |
| `extract_vectors.py` | EVT startup .Sからvector table仕様(`CH32_IRQ`/`CH32_RSV`/`CH32_JMP`行)を抽出 |
| `compare.py` | EVT版ELFと統合版ELFの等価性検証: (1)両者のvector tableが抽出仕様と一致 (2)handle_resetのCSR書き込み集合が一致(mtvec/mepcはシンボル書き込みのため存在のみ確認) |
| `run_check.sh` | 3 family(V00X/X035/V307 D8C)のビルドと検証を一括実行 |

## 使い方

```sh
CH32_MIRROR_ROOT=/home/mt/dev_wch \
CH32_GCC_BIN=/path/to/xpack-riscv-none-elf-gcc-14.3.0-1/bin \
./run_check.sh /tmp/w2-work
```

## crt0_ch32.Sのパラメータ

| define | 内容 | 例 |
|---|---|---|
| `CH32_VECTORS` | vector includeファイル名(引用符付き) | `\"vectors_v00x.inc\"` |
| `CH32_MSTATUS_INIT` | mstatus初期値 | 0x1880(V2) / 0x88(V3/V4) / 0x6088(+FPU) / 0x688(+RVV) |
| `CH32_INTSYSCR_INIT` | CSR 0x804初期値 | 0x3 / 0x7 / 0x0b / 0x0f |
| `CH32_CORECFGR` | (任意)CSR 0xbc0初期値 | 0x1f / 0x21 / 0x123703E1 |
| `CH32_CSR_BC1` | (任意)CSR 0xbc1初期値 | 0x1 / 0x7 |
| `CH32_CSR805_CLR` | (任意)CSR 0x805でクリアするbit | 0x100(V407) |
| `CH32_HIGHCODE` | (任意)`.highcode`のRAMコピーを有効化 | − |
| `CH32_NO_INIT_ARRAY` | (任意)C++グローバルコンストラクタ(`.init_array`)呼び出しを無効化 | − |

## 既知の未対応

- H417 V5Fのloadcode/RAM実行boot(R-01参照。対応時は`CH32_LOADCODE`軸を追加)
- V103のj命令テーブル(`CH32_JMP`は実装済みだがmtvecモード検証が未了)
- `.init`一体型family(V00X等)のEVT linker scriptには`.vector`出力sectionがないため、統合startupには`.vector`ルール入りldが必要(`run_check.sh`が生成する2行差分)
