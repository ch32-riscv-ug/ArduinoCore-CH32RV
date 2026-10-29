# examples のビルド規則

同梱 examples の profile は [sync_example_profiles.py](../tools/generate/sync_example_profiles.py) が生成します。
テスト suite やベンチ設定を生成元にしません。

```sh
uv run --no-project python tools/generate/sync_example_profiles.py
uv run --no-project python tools/generate/sync_example_profiles.py --check
```

profile はコンパイル条件の宣言であり、実機検証済み・公開済みなどの状態を示しません。
profile 用の代表 generic board は生成器で明示し、ベンチの有無や検証結果とは独立させます。
ANY の flash/RAM 下限、variant の peripheral capability から要件を満たす候補を選びます。
profile の存在を、その系列の全 part/package/route の動作保証にしません。

example が追加の機能を必要とする場合は .ino に宣言します。

```c
/* requires: USBFS, flash=32K */
```

capability は生成 header の CH32RV_CLKEN_<name>_ADDR に対応します。
flash=/ram= は ANY menu entry に対する下限です。未知の capability/key はエラーにします。

製品ボードのテンプレートのように特定の板だけのための example は、板を宣言します。代表 board ごとの profile の代わりに、
その fqbn（`ch32-riscv-ug:ch32rv:` より後ろ）の profile を1つだけ作ります。boards.txt に無い板や menu の値はエラーにします。

```c
/* board: CH32X035:pnum=CH32X035F8U6 */
```
silicon capability はコア API の実装範囲や実機設備とは別です。

sketch.yaml は全文生成してソース管理に含めます。
platform version は platform.txt を参照し、依存解決用の index URL を設定します。
テスト対象 core の作業ツリーと、利用者が導入する version 固定 profile の検証は区別します。
生成整合性、compile、相互運用・実機動作の保証範囲は [カバレッジ](test-coverage.ja.md)を参照してください。

## 例を足すときの決まり

- `.ino` は英日それぞれ 1 行の概要から始め、重要な操作・状態遷移・エラー処理を初心者向けに説明する。
  本当に短い注記以外は `// en:` と `// ja:` を別行にする。使い方・配線・長い注意書きは `README.md` / `README.ja.md` へ置く。
- README は目的、必要な部品・相手、配線、操作、期待する結果、失敗時に確認する点を日英でそろえる。
  任意の外部ライブラリや PC 側ツールが必要なら導入方法も書き、更新される対応表や内部実装の数値は重複して固定しない。
- 対応する周辺回路・ピン・容量を使うビルド条件と `sketch.yaml` を整備する。
  最小構成でのビルドと実機・相手側の確認を行い、条件付きのものは条件を明記する。
- 新しい例を [日本語一覧](examples.ja.md) / [英語一覧](examples.md) へ追加し、ライブラリの README からもたどれるようにする。
- 成功だけでなく、入力なし・相手不在・時間切れ・再接続・バッファ満杯など、その例に関係する失敗を扱う。
- 製品ボード別のテンプレート（`libraries/CH32RV/examples/Board*`）は、`board:` の宣言でその板だけビルドする。板が増えたら同じ形で足す。
