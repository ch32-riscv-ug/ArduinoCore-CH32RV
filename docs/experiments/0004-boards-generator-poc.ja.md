# 実験0004: device-data → boards.txt/ld生成と26 SKU compile matrix

実施日: 2026-08-19
対象question: Q-011/Q-014(consumer形式の試作)、Q-015、R-03の設計検証
実装: [tools/generate/](../../tools/generate/README.ja.md)
実施環境: WSL2 Linux x86_64、arduino-cli、xPack riscv-none-elf-gcc 14.3.0-1。実機なし(compile/linkのみ)

## 目的

`ch32-device-data/tables/products.csv`を入力に、R-03で推奨した「family単位board+pnumメニューに全型番」のboards.txtとSKU別linker scriptを生成し、prototype platform(実験0003)で全SKUのcompileが通ることを確認する。

## 方法と結果

1. **生成**: generator(`generate.py`)がproducts.csvのCH32V006 family 26行から以下を生成
   - `boards.txt`: board 1個+pnum 26項目(package/容量入りラベル、`build.board`/`build.series`(デバイス選択マクロ、V007/M007→`CH32V007_M007`)/`build.ldscript`/`upload.maximum_*`)
   - ldファイル3本(ユニークな(FLASH,SRAM)組合せ: 16K/4K、32K/6K、62K/8K。MEMORY+共通sections.ldのINCLUDE)
   - 生成物ヘッダにsource tablesのgit commit(`8c09ab44…`)を記録。タイムスタンプなし
2. **idempotency**: `--check`モードで再生成一致を確認(CIのgenerated-sync jobの種)
3. **compile matrix**: compile_matrix.pyを「boards.txtの全pnumを列挙してcompile」に拡張し、**26/26 SKUでBlinkのcompile/link成功**(exit 0)。サイズ上限がSKUごとに正しく反映される(16K SKUは2%、62K SKUは0%表示。例外SKUのCH32V006F4U6も16K/4Kとして正しく生成)

## 結論

- **「device databaseを正本にboards.txt/ldを機械生成し、生成物をcommitしてCIで一致検証する」パイプラインは成立する**(R-03/R-12の設計を実証)
- pnum 26項目のメニューはarduino-cli上で問題なく機能する(IDE 2.x GUIは未確認)
- ldの共有粒度「ユニークな(FLASH,SRAM)組合せ」で26 SKU→3ファイルに集約できた

## 再現手順

```sh
python3 tools/generate/generate.py \
  --tables <ch32-device-data>/tables --platform . --check
CH32_GCC_BIN=<xpack bin> tests/compile/compile_matrix.py /tmp/w4-work
# どちらもexit 0
```

## 残る未検証事項

- 他family(特にライン差のあるV20x/V307)のFAMILY_CONFIG追加と、family横断generatorの構造
- variant(pin map)生成(pins.csv/pin_functions.csvの利用はArduinoピン設計合意後)
- device-dataの**固定release**を入力にするlock機構(現状はローカルsibling treeのHEADを参照し、commitを記録するだけ)
- IDE 2.x GUIでの26項目メニューのUX確認
