# tests

[テスト計画](TEST_PLAN.ja.md)と[カバレッジ](../docs/test-coverage.ja.md)を参照してください。
ここで実行できるテストは実機不要です。実機用の設備別ディレクトリは README のみの雛形で、sketch・接続先・実機 fixture はありません。

```sh
cd tests
uv sync --locked
uv run pytest
```

通常の pytest は unit/ と build/ だけを収集します。repository root からは
`uv run --project tests pytest` で同じ検査を実行できます。
Arduino の書込み・monitor や、通常の Arduino data/sketchbook の変更は行いません。
build/ の固定 source 検査もボード・CH32 toolchain・ネットワーク不要です。
新しい依存はこのディレクトリの pyproject.toml と uv.lock で管理し、旧 harness を import しません。

| 場所 | 内容 |
|---|---|
| unit/ | 配置・収集範囲の安全性 |
| build/ | 固定 source のオフライン整合検査 |
| single/、loopback/、peer/、instrumented/、manual/ | 設備別の雛形のみ |
| fixtures/ | 論理信号、必要 capability、安全条件の共有設計 |
| harness/、diagnostics/、sketch_support/ | 補助機能の配置先のみ |

通常の検査・CI・配布ツールは旧 suite を読み込まず、旧フォルダがなくても動きます。
examples の生成は tools/generate、package 確認は tools/index の独立したツールです。
未整備の full compile matrix、startup 等価性、size baseline、実機契約は
カバレッジに明示し、この suite の成功で置換完了とは扱いません。
