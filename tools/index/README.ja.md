# Board Manager 配布物の生成

この directory は platform archive、package index、tool の取得を扱います。

| ファイル | 役割 |
|---|---|
| `gen_index.py` | allowlist から platform archive と package index を生成する |
| `tools_xpack_gcc.json` | compiler の host 別 URL、size、checksum |
| `tools_ch32rv.json` | uploader / discovery / monitor の host 別 URL、size、checksum |
| `ch32rv_chips.csv` | 同梱 `ch32rv` が受理する chip 名 |
| `fetch_tools.py` | 固定 tool と device-data を `.tools/` へ取得する |
| `install_check.py` | 一時 server と空の Arduino data directory で install を検証する |

`tools_probe_rs.json` と `probe_rs_targets.csv` はテスト側の移行が終わるまで残っている互換入力で、
platform archive の tool dependency ではありません。

## Archive の内容

`gen_index.py` の `PLATFORM_ENTRIES` が唯一の allowlist です。repository 全体を archive に入れません。
entry が tree に存在しない場合は省略し、`REQUIRED_ENTRIES` が欠ける場合は失敗します。

作業 tree の `platform.txt` は `compiler.path=` を空にし、PATH または明示指定で開発できます。
archive 作成時だけ、Board Manager が導入した xPack tool の path に書き換えます。

## 生成

```sh
uv run --no-project python tools/index/gen_index.py \
  --platform . --out dist \
  --base-url https://example.invalid/releases/download/vX.Y.Z \
  --tools github \
  --merge package_ch32-riscv-ug_index.json
```

`version` と board 一覧は `platform.txt` / `boards.txt` から読みます。手入力の複製は持ちません。
既存 index を更新する場合は `--merge` を使い、過去 version を参照する installation を壊さないようにします。

local archive で tool も配信するときは `--tools local --local-tools <name,...>` を使います。

## 固定 tool の取得

```sh
uv run --no-project python tools/index/fetch_tools.py
uv run --no-project python tools/index/fetch_tools.py --print-env
```

取得物は JSON に記録された size と SHA-256 で照合します。version、URL、checksum を別の文書へ重複記載しません。

compiler の判断理由は [ADR-0002](../../docs/adr/0002-toolchain-distribution.ja.md)、`ch32rv` の責務は
[ADR-0008](../../docs/adr/0008-upload-strategy.ja.md) を参照してください。
