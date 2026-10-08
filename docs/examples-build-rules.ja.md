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
silicon capability はコア API の実装範囲や実機設備とは別です。

sketch.yaml は全文生成してソース管理に含めます。
platform version は platform.txt を参照し、依存解決用の index URL を設定します。
テスト対象 core の作業ツリーと、利用者が導入する version 固定 profile の検証は区別します。
生成整合性、compile、相互運用・実機動作の保証範囲は [カバレッジ](test-coverage.ja.md)を参照してください。
