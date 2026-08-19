# W-3 prototype: 最小Arduino platform

状態: proof of concept(2026-08-19)。compile専用。リリース対象ではありません。
関連: [環境整備計画](../../docs/infrastructure.ja.md) W-3、[R-03](../../docs/research/board-variants-and-menus.ja.md)、[R-15](../../docs/research/local-install-and-test-env.ja.md)方式A

## 目的

arduino-cliのsymlink方式で、暫定FQBN(`ch32-riscv-ug:ch32v:CH32V00X:pnum=...`)からBlinkのcompile/linkを一周させ、以下のメカニズムを検証する。

- boards.txtのpnumメニュー → build.board/series/ldscript/march/mabi/startup定義の注入
- 統合startup(crt0_ch32.S)+own linker script+own vector includeによるvendorファイル非依存のビルド
- 「toolchain未installでも`--build-property compiler.path=`で差し込める」開発フロー

## 暫定ID(Q-015の仮決め。公開IDはQ-017のADRで確定)

- packager: `ch32-riscv-ug`(ユーザーグループ。WCH公式ではない。lang-ship系とは別名前空間)
- architecture: `ch32v`
- FQBN例: `ch32-riscv-ug:ch32v:CH32V00X:pnum=CH32V006K8U7`

## 使い方

```sh
CH32_GCC_BIN=/path/to/xpack-riscv-none-elf-gcc-14.3.0-1/bin \
./test_compile.sh /tmp/w3-work
```

サンドボックス化した`ARDUINO_DIRECTORIES_*`を使うため、実環境の`~/.arduino15`や`~/Arduino`には触れない。

## 構成

```
ch32v/
  platform.txt              最小recipe(c/cpp/S/ar/link/objcopy/size)。compiler.path未指定時はPATH
  boards.txt                生成物(prototypes/generator/generate.py、pnum 26項目)。手編集禁止
  cores/arduino/
    Arduino.h main.cpp wiring_stub.c   compile専用スタブAPI
    crt0_ch32.S             正本は ../startup/crt0_ch32.S(同期が必要。将来は生成/共有化)
    vectors_ch32v00x.inc    割込み番号表の自前転記(EVT抽出仕様との一致をdiffで検証済み)
  variants/CH32V00X/
    pins_arduino.h          スタブ
    sections.ld             own実装の共通セクション定義(init_array系symbolを含む)
    ch32v00x_{16k_4k,32k_6k,62k_8k}.ld   生成物(ユニークなFLASH/SRAM組合せ別)
```

`test_compile.sh`はboards.txtの全pnum(26 SKU)をcompileするcompile matrixとして動く。

## 既知の制限(実装時に解消する)

- グローバルコンストラクタはcrt0が`.init_array`ループで呼び出す(`CH32_NO_INIT_ARRAY`で無効化可)。test_compile.shが「sketchのctorが.init_arrayに載る+crt0に呼出ループがある」ことを静的検査するが、**実行はHIL待ち**
- API/pinはスタブ。upload/デバッグrecipeなし
- vendorヘッダ・SPLを一切含まないため、実ペリフェラル操作はできない
- `crt0_ch32.S`が`prototypes/startup/`と二重管理(prototype段階の割り切り)
