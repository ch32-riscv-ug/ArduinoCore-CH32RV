# 実験0003: 最小platform prototypeによるarduino-cli Blink compile

実施日: 2026-08-19
対象question: Q-015(暫定packager/FQBN)、Q-012(own startup/ld/vector構成)、R-15方式A
実装: [tests/compile/](../../tests/compile/README.ja.md)
実施環境: WSL2 Linux x86_64、arduino-cli(ローカルinstall済み)、xPack riscv-none-elf-gcc 14.3.0-1(実験0001と同一)。実機なし(compile/linkのみ)

## 目的

vendorファイルを一切含まない最小platform(own crt0+own linker script+own vector include+スタブAPI)で、arduino-cliのsymlink方式からBlinkのcompile/linkを一周させ、boards.txtのpnumメニュー機構と暫定FQBNを検証する。

## 検証した構成

- 暫定FQBN: `ch32-riscv-ug:ch32v:CH32V00X:pnum=CH32V006K8U7`(packager=ch32-riscv-ug、architecture=ch32v)
- pnumメニュー2項目(CH32V006K8U7=62K/8K、CH32V002F4U6=16K/4K)が、`build.board`/`build.series`(-D注入)/`build.ldscript`/`upload.maximum_*`を切替
- startupは統合crt0(実験0002と同一方式)。vector includeは**EVTからのコピーではなく割込み番号表の自前転記**で、EVT抽出仕様とのdiff一致を事前確認済み
- linker scriptはown実装(SKU別MEMORY + 共通`sections.ld`のINCLUDE構成、`__init_array_*`シンボル提供)
- toolchainは`--build-property "compiler.path=<xPack bin>/"`で注入(platform未installのtool不要)
- `ARDUINO_DIRECTORIES_USER/DATA/DOWNLOADS`をサンドボックス化し、実環境の`~/.arduino15`等に触れない

## 結果

- `arduino-cli board listall`にboardが表示され、**両pnumでBlink(グローバルコンストラクタ入り)のcompile/link成功**
- サイズ報告が正しくSKU連動: 「Sketch uses 460 bytes (0%) … Maximum is 63488 bytes」/「(2%) … Maximum is 16384 bytes」
- ELF: text 456 / data 4 / bss 520(`.init_array`にMarkerコンストラクタが配置されることを確認)
- 修正した問題: (1) board直下に`build.board`既定がないとarduino-cliが警告(既定を追加) (2) `.init_array`(書込属性)がFLASHのRXセグメントに同居するためbinutils 2.43+がRWX警告 → `--no-warn-rwx-segments`で抑止し、実装時にPHDRSで見直す旨をコメント化

## 結論

- **symlink方式(R-15方式A)+compiler.path注入の開発フローは成立する**
- pnumメニューからld/march/定義を切り替えるR-03の設計はarduino-cliの実挙動で確認できた
- 暫定ID(`ch32-riscv-ug:ch32v`)で問題は出ていない。公開IDはQ-017で確定する

## 再現手順

```sh
CH32_GCC_BIN=<xpack-riscv-none-elf-gcc-14.3.0-1>/bin \
uv run tests/compile/compile_matrix.py /tmp/w3-work
# exit 0 = 両pnumのcompile成功
```

## 残る未検証事項

- グローバルコンストラクタの**実行**(crt0が.init_arrayを未呼出。own crtの必須作業として実装時に対応)
- R-15方式B(ローカルHTTP+Board Manager経由install)。tool定義とpackage index生成が前提(W-5)
- Windows/macOS host(GitHub Actions matrixで実施予定)
- IDE 2.x GUIでのメニュー表示(CLIのみ検証済み)
