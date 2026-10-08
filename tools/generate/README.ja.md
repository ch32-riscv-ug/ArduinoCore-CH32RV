# Device data からの platform 生成

`generate.py` は `ch32-device-data/index/` を入力に、次を生成します。

- `boards.txt`
- generic board の `variants/<SERIES>/pins_arduino.h`
- 型番ごとの linker script
- core が使う vector、IRQ、EXTI、clock 関連 header
- `vendor/ch32-device-data.lock.toml`

生成物は通常ビルドを offline に保つため commit します。手編集はしません。

## 入力の取得

固定済み revision を `.tools/ch32-device-data` へ取得するには次を実行します。

```sh
uv run --no-project python tools/index/fetch_tools.py --tool ch32-device-data
```

別 revision を採用するときは、その checkout を `--tables` に渡します。生成器は
`index/manifest.csv` と `index/VERSION` を検証し、consumer surface 以外は読みません。

## 生成と確認

```sh
uv run --no-project python tools/generate/generate.py \
  --tables .tools/ch32-device-data --platform .

uv run --no-project python tools/generate/generate.py \
  --tables .tools/ch32-device-data --platform . --check

uv run --no-project python tools/generate/generate.py \
  --tables .tools/ch32-device-data --platform . --check --diff
```

`--check` はファイルを書き換えません。`--check --diff` は commit 済み生成物との差分も表示します。

## 更新時に確認すること

同梱 examples の profile は、テストや実機ベンチに依存しない生成器で同期します。

```sh
uv run --no-project python tools/generate/sync_example_profiles.py
uv run --no-project python tools/generate/sync_example_profiles.py --check
```

要件の宣言と対象選択は [examples のビルド規則](../../docs/examples-build-rules.ja.md)を参照してください。

- 既存の board ID、pin 値、既定 route、memory size が意図せず変わっていないか
- 新しい series / part が適切な ISA、ABI、vector variant、clock 設定を使うか
- lock file の commit、schema version、manifest hash が入力と一致するか
- 生成物に時刻や checkout path のような非決定的な値が入っていないか

生成物 header に上流 commit を重複記録しません。入力の同一性は lock file が一か所で表します。
設計上の理由は [ADR-0001](../../docs/adr/0001-device-data-repository.ja.md) と
[ADR-0005](../../docs/adr/0005-board-structure-and-fqbn.ja.md) を参照してください。
