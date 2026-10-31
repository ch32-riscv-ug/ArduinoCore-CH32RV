# ArduinoCore-CH32RV

[English](README.md)

WCH CH32 RISC-V マイコン向けの、コミュニティ管理による Arduino コアです。
WCH の公式プロジェクトではありません。

## インストール

Arduino IDE の「ファイル → 基本設定 → 追加のボードマネージャの URL」に次を追加し、
ボードマネージャから「CH32 RISC-V」をインストールします。

```text
https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

arduino-cli では次のようにします。

```sh
arduino-cli core install ch32-riscv-ug:ch32rv \
  --additional-urls https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

toolchain と書き込みツール `ch32rv` も一緒にインストールされます。書き込み時は IDE の port で
WCH-Link、OEP probe、または対応ボードのブートローダを選び、「書き込み」を実行します。

系列ごとのビルド、書き込みの実装範囲は
[系列ごとの実装範囲](docs/support-status.ja.md) を参照してください。

## 設計上の前提

- 対象は CH32 の RISC-V 系列です。Arm 系の CH32F と無線 SoC は扱いません。
- Arduino API の基盤には固定した ArduinoCore-API 1.5.2 を使用します。
- startup、CRT、vector table、linker script と周辺機能の実装はこのプロジェクトが所有します。
- 型番、メモリ、pin、割り込み等のデータは `ch32-device-data` の固定 revision から生成します。
- 通常ビルドはネットワークへアクセスせず、生成物を使用します。
- コアはベアメタルです。RTOS は同梱しません。
- 書き込みと monitor は同梱の `ch32rv` に一本化します。

判断理由は [ADR](docs/adr/README.ja.md) に、利用者が必要な有効な仕様だけを記録しています。

## ビルドメニュー

| メニュー | 既定 | 内容 |
|---|---|---|
| Part Number (`pnum`) | `ANY` | 型番。`ANY` は系列内の最小 flash/RAM を宣言するため、同じ系列のどの型番にも載せられる |
| CPU Clock (`clock`) | 系列ごと | システムクロック。項目名に周波数と、その周波数で使えるもの（USB、Ethernet）を書いてある。既定は V20x / V30x が 96 MHz、V103 が 72 MHz、X035 が 48 MHz、V00x が 24 MHz、L103 / M103 が 8 MHz、M030 が 72 MHz、V205 が 160 MHz（M030 と V205 は実機で確かめて EVT の既定に上げた。L103 / M103 の PLL の項目は「not checked」で、L103 は 72 / 96 MHz で動いている間、OEP probe が接続できない（WCH-LinkE はできる）。試すときは [起動待ち](docs/upload-routes.ja.md#接続できなくなるイメージを試すとき起動待ち) を付ける）。V208 / V203RB の Ethernet は `60MHz (Ethernet, no USB)` を選ぶ。V407 / V467 は 200 MHz（120 / 60 / 20 MHz も選べる。V407WEU6 で実機確認）。X315 は 20 MHz で、240 MHz も選べる（HSI から USBHS PLL。X315MCU6 で実機確認）。H41x にはまだ無い。いまのクロックは `getCpuFrequencyMhz()` / `getApbFrequency()`（arduino-esp32 と同じ）で取れる。実行中に変える API は無い |
| Clock Source (`clocksrc`) | `hsi` | `hse` にすると、CPU Clock の同じ周波数を外付けの水晶（HSE）から作る。水晶の周波数は EVT の前提のもの（V00x 24 MHz、V103 / V20x / V205 / V30x / L103 8 MHz）で、ベンチの基板はどれもそれだった。起動時に水晶が立たなければ（1〜2.5 ms で立つところを約 10 ms 待つ）、止まらずに HSI で同じ周波数を作る。どちらで動いているかは `CH32RV.clockSource()` で分かる。USB HS と Ethernet の HSE の周波数の既定も、このメニューの値になる。V208・M030・X315・V407・H41x にはまだ無い（[A の機能の全体設計](docs/peripheral-a-design.ja.md) 5.5） |
| Optimize (`opt`) | `oslto` | `-Os` に LTO。flash が 9〜24% 減り、速さは同じ（X035 で実測）。`osltosr` は `-msave-restore` も足してさらに 4〜5% 減るが、関数呼び出しの多い処理（楕円曲線暗号など）が 7〜9% 遅くなる。`osstd` は LTO なし |
| Floating Point (`rtlib`) | `fltf_pi` | 浮動小数を double と float のどちらで計算するか、printf の `%f`、`parseFloat` の計算、newlib の printf / scanf の浮動小数 |
| Reset Pin (`rstpin`) | 系列の出荷時の値 | リセット端子をオプションバイトで決める系列（V002〜V006 は出荷時にリセット、V007・M007・X033・X035・M030・X305・X315 は GPIO）。出荷時の値の項目は何もせず、チップはそのときの設定のまま。もう一方を選ぶと、起動時にコアがオプションバイトを比べ、違えば一度だけ書き換えてリセットする（V003 で 276 B 増える）。ch32rv などで変えても、その項目で作ったスケッチが動くたびに戻る。UIAPduino にはメニューが無い（HID ブートローダーが出荷時のリセット端子を使う） |
| Erase Flash (`erase`) | `keep` | 書き込みで消す範囲。Keep Store は `CH32RV_FLASH_STORE` で確保した末尾を残す（ch32rv が退避して書き戻す。V003 の 16 KB で約 0.8 秒、V307 の 96 KB で約 1 秒増える）。All Flash（`chip`）はチップ全体を消す。H41x の DFU では All Flash を選ぶと書き込みが断られる。UIAPduino にはメニューが無い（HID だけで書く） |

printf の系統（`Serial.printf`、`printf`、`sprintf`、`snprintf`）、`Serial.print(float)`、`String(float)` はコアの書式化で動きます。
既定（`fltf_pi`）は AVR と同じく float で計算し、printf の `%f` も出します。double で計算するより、FPU のない系列で数 KB、V30x・X3x5・H41x で十数 KB 小さくなります。
15 桁の精度が要るとき（GPS の緯度経度など）は `dbl_*` / `dblf_*` を選びます。小数を使わずに printf を使うときは、`%f` なしの `flt_*` で約 0.9 KB（FPU のある系列で 1.5 KB）小さくなります。FPU のない系列では、小数の表示に soft-float を使いません（float の計算を値のビットに対して行います）。
小数も printf も使わなければ、どの選択肢でも大きさは同じです。
`strftime()`、`gmtime_r()`、`localtime_r()`、`tzset()` もコアのもので、newlib のものより 2〜10 KB 小さくなります（出す文字は同じです）。
`Serial.parseFloat()` の計算は、double（`_pd`：いちばん近い float）、整数（`_pi`：11 桁までいちばん近い float、double の計算なし）、float（`_ps`：いちばん小さいが、最後の 1 ビットがずれることがある）から選べます。
選択肢と費用、C との違いは [printf と浮動小数](docs/printf-float.ja.md) にあります。

## リポジトリ構成

リポジトリのルートが Arduino platform ディレクトリです。

```text
platform.txt          ビルド、書き込み、monitor、debug の recipe
boards.txt            生成物。手編集禁止
cores/arduino/        コア本体と ArduinoCore-API の固定コピー
variants/             pin 定義と linker script。生成物
libraries/            同梱ライブラリと examples
tools/generate/       ch32-device-data からの生成
tools/index/          Board Manager 配布物の生成
tools/vendor/         第三者ソースの固定内容を検証するツール
vendor/               上流 revision、hash、license の lock file
docs/                 現行仕様、利用手順、ADR
tests/                実機不要の検査と新テストの雛形
```

Board Manager の配布物は allowlist 方式で作られます。正確な対象は
`tools/index/gen_index.py` の `PLATFORM_ENTRIES` が正本です。

## ドキュメント

- [ドキュメント一覧](docs/README.ja.md)
- [サンプルスケッチ案内](docs/examples.ja.md)（目的から例を選び、配線・API・改造時の注意を確認）
- [スケッチの組み立て方](docs/sketch-guide.ja.md)（割り込み、待たない書き方、callback。Blink の次に読む）
- [系列ごとの実装範囲](docs/support-status.ja.md)
- [ペリフェラル対応状況](docs/peripheral-support.ja.md)
- [USB Device・Host・PD とサンプルの使い方](docs/usb.ja.md)
- [デバッグ出力](docs/debug-output.ja.md)
- [デバッガ](docs/debugger.ja.md)
- [フラッシュ使用量の調べ方](docs/flash-size.ja.md)
- [設計判断](docs/adr/README.ja.md)

## ライセンス

本プロジェクトが作成したコードと文書は [MIT License](LICENSE) です。
第三者成果物には各成果物のライセンスが適用されます。

| path | 由来 | ライセンス |
|---|---|---|
| `cores/arduino/api/` | ArduinoCore-API 1.5.2 | LGPL-2.1-or-later |
| `libraries/CH32RVUsbHost/src/CH32RVUsbHostCdc.cpp` の USB シリアルチップの手順 | TinyUSB の cdc_host.c（Linux のドライバ由来）を書き直したもの | MIT |

固定 revision とファイル hash は `vendor/*.lock.toml` に記録し、検証ツールで一致を確認します。
