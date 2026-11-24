# 開発ワークフロー

テストの保証対象・設備・環境分離は [テスト計画](../tests/TEST_PLAN.ja.md)、
実装とテスト定義の範囲は [カバレッジ](test-coverage.ja.md)を正本にします。

## 実機不要の検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml
uv run --no-project python tools/generate/sync_example_profiles.py --check
```

device-data の取得・再生成確認は [生成器の手順](../tools/generate/README.ja.md)に従います。
vendor source の hash と upstream commit の由来は別の検証です。

## package と version

配布物の導入確認は tools/index/install_check.py を独立して実行します。
一時ディレクトリ内の Arduino data/user/downloads へ導入し、ツール自身が持つ compile-only sketch を使用します。
通常の sketchbook、実機 port、ベンチ設定を必要としません。

version 更新は tools/index/bump_version.py で platform.txt と同梱 example の pin を更新します。
example profile の同期は tools/generate/sync_example_profiles.py が担当します。
退避したテストの sketch.yaml は version 更新や生成チェックの対象にしません。

利用者に見える変更は、コミットのときに CHANGELOG.md の `## Unreleased` へ `- (EN) ...` と `- (JA) ...` の組で書きます。
リリースの workflow が tools/index/prepare_release.py でそれを `## X.Y.Z` に移し、その節を GitHub の Release の本文にします。
リリースは Actions の Release を手動で動かし、`bump` のプルダウンで patch / minor / major を選びます（最新の `vX.Y.Z` のタグから上げる）。
決まった版にしたいときは `version` に X.Y.Z を書きます（`bump` より優先）。`v<版>` のタグを push しても動きます。

## CI の境界

CI は新 suite、生成整合性、vendor provenance、package 導入を実行します。
未整備の全対象 compile matrix、startup 比較、size baseline、実機契約を、
これらの成功から保証済みとは判断しません。

実機 runner の実装では、信頼した入力、一意な接続先、共有設備の排他、
開始時の状態正規化、安全な終了を必須にします。具体的な設備契約はテスト計画で定義します。
