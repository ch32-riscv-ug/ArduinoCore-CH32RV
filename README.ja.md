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

toolchain（ch32-riscv-ug/ch32rv-toolchain：clang と lld、picolibc、compiler-rt、libc++、GDB）と書き込みツール `ch32rv` も一緒にインストールされます。書き込み時は IDE の port で
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
| CPU Clock (`clock`) | 系列ごと | システムクロック。項目名に周波数と、その周波数で使えるもの（USB、Ethernet）を書いてある。既定は V20x / V30x が 144 MHz、V205 が 160 MHz、V407 / V467 が 200 MHz、X315 が 240 MHz、L103 / M103 が 96 MHz、V103・M030 が 72 MHz、X035 が 48 MHz、V00x が 24 MHz。APB の分周は項目ごとにコアが決め（ADC と I2C の上限に収める）、Serial・SPI・Wire・タイマーはそれぞれのバスのクロックで計算する。V208 / V203RB の Ethernet は `120MHz` か `60MHz (Ethernet, no USB)` を選ぶ（ほかの項目で `ETH.begin()` を呼ぶとビルドエラー）。ベンチの DUT で全部の項目の時計・Serial・SPI・ADC を確かめた（2026-11-13、USB は項目ごとには見ていない）。X035 の 8 MHz は ADC のクロックがデータシートの下限（3 MHz）に届かないので「no ADC」で、`analogRead()` などを呼ぶとビルドエラーになる。L103 は 72 / 96 MHz で動いている間、OEP probe が接続できない（WCH-LinkE はできる）。試すときは [起動待ち](docs/upload-routes.ja.md#接続できなくなるイメージを試すとき起動待ち) を付ける。H41x と X305 にはこのメニューが無い（H41x は CPU Core で決まる）。いまのクロックは `getCpuFrequencyMhz()` / `getApbFrequency()`（arduino-esp32 と同じ）で取れる。実行中に変える API は無い |
| CPU Core (`core`) | `v5f` | H41x でスケッチを動かすコア。V5F は 400 MHz、V3F は 100 MHz。周辺の回路とタイマーはどちらでも HCLK（100 MHz）で動き、`F_CPU` はスケッチのコアのクロック、`CH32RV_HCLK_HZ` は HCLK。V5F では `csrr mcycle` が例外になるので、時間は `micros()` で測る。デバッガで V5F を見るときは `ch32rv --core 1 dbg ...`、終わったら `dbg resume --all` で両方のコアを走らせる（[h417-cores-design](docs/h417-cores-design.ja.md)） |
| Clock Source (`clocksrc`) | `hsi`（V208・H41x は `hse`） | `hse` にすると、CPU Clock の同じ周波数を外付けの水晶（HSE）から作る。水晶の周波数は EVT の前提のもの（V00x 24 MHz、V103 / V20x / V205 / V30x / L103 8 MHz）で、ベンチの基板はどれもそれだった。V208 は 32 MHz で、10BASE-T が水晶の精度（±100 ppm）を求めるので `hse` を既定にした。HSI でもベンチではリンクしたが規格の外（8 MHz の項目は PLL を使わず HSI のまま）。起動時に水晶が立たなければ（1〜2.5 ms で立つところを約 10 ms 待つ）、止まらずに HSI で同じ周波数を作る。どちらで動いているかは `CH32RV.clockSource()` で分かる。USB HS と Ethernet の HSE の周波数の既定も、このメニューの値になる。H41x は 25 MHz で `hse` が既定（どのボードも USB のために水晶を持ち、HSI の約 1% のずれが UHSIF や HSADC の速さにそのまま出ていた。H417 で確認、2026-11-14）。M030・X315・V407 にはまだ無い（[A の機能の全体設計](docs/peripheral-a-design.ja.md) 5.5） |
| Optimize (`opt`) | `ozlto` | `-Oz` に LTO。いちばん小さい（LTO で flash が 9〜24% 減り、速さは同じ。X035 で実測）。`oslto`（`-Os`）は約 8% 大きく、そのぶん速い（L103 の 8 MHz で、割り込みを含む区間が 55 µs から 40 µs）。`o2lto`（`-O2`）と `o3lto`（`-O3`）は速さを優先する。`ozltosr` は `-msave-restore` も足してさらに 4〜5% 減るが、関数呼び出しの多い処理（楕円曲線暗号など）が 7〜9% 遅くなる。`ozstd` は LTO なし |
| Floating Point (`rtlib`) | `fltf` | 浮動小数を double と float のどちらで計算するか、printf の `%f`、picolibc の printf / scanf の浮動小数 |
| parseFloat (`parsefloat`) | `auto` | `Serial.parseFloat()` の計算。`auto` は Floating Point に合わせる（Float なら整数、Double なら double） |
| USART1〜10、I2C1〜2、SPI1〜3（`usartN`、`i2cN`、`spiN`） | `auto` | その周辺の端子の決め方。AUTO は `begin()` で決める（既定の端子か、`setPins()` で選んだもの）。FIX は端子をビルドで決める（既定でない端子を使うとき、`setPins()` より Serial で 472〜1028 B、Wire で 28〜100 B 小さい）。Disable はその周辺をリンクしない（X035 で Serial 以外の USART を外すと 456 B 減る）。`Serial`、`Serial1`… の名前は、有効な周辺を番号順に並べたものなので、Disable にすると後ろの名前が詰まる。I2C と SPI の Software は、その名前（`Wire`、`SPI`…）を、その周辺の既定の端子の SoftWire / SoftSPI にする（周辺の回路はリンクしない。V003 の Wire の例で 2996 B、ハードの I2C は 4008 B） |
| Flash / RAM Split (`ramsplit`) | `256k_64k` / `512k_200k`（工場出荷） | SRAM を、待ちなしの flash の写しと RAM に分ける割合。V303 / V305 / V307 / V317（320 KB、5 通り）と、V407 / V467 の RET6・VET6・WEU6（512 KB + 200 KB か 576 KB + 136 KB）（[support-status.ja.md](docs/support-status.ja.md)）。工場出荷でない値は、起動時にコアがオプションバイトを書き換える |
| VIO18 Supply (`vio18`) | `hw` | H41x の VIO18 の電圧。`hw` は基板の XO の strap のまま。ほかは起動時にコアが設定する |
| Reset Pin (`rstpin`) | `keep`（チップの設定のまま） | リセット端子をオプションバイトで決める系列（V002〜V006 は出荷時にリセット、V007・M007・X033・X035・M030・X305・X315 は GPIO）。`keep` は何もせず、チップはそのときの設定のまま。`reset` と `gpio` は、出荷時の値の側も含めて、起動時にコアがオプションバイトを比べ、違えば一度だけ書き換えてリセットする（V003 で 276 B 増える）。ほかのスケッチや ch32rv で変えたチップも、その項目で作ったスケッチで戻る。UIAPduino にはメニューが無い（HID ブートローダーが出荷時のリセット端子を使う） |
| Watchdog (`iwdg`) | `keep`（チップの設定のまま） | IWDG を、`wdtEnable()` で始める（`sw`、工場出荷）か、電源投入から動かす（`hw`）か。`sw` と `hw` はどちらも起動時にオプションバイトを比べて書き換える（V003 で 264 B 増える。Reset Pin も選ぶときは、書き換えのコードを共にするので 80 B）。`keep` は何もしない。V00x、M007、X03x、X3x5、V20x、V205、V30x、L103 / M103、V407 / V467 |
| Erase Flash (`erase`) | `keep` | 書き込みで消す範囲。Keep Store は `CH32RV_FLASH_STORE` で確保した末尾を残す（ch32rv が退避して書き戻す。V003 の 16 KB で約 0.8 秒、V307 の 96 KB で約 1 秒増える）。All Flash（`chip`）はチップ全体を消す。H41x の DFU では All Flash を選ぶと書き込みが断られる。UIAPduino にはメニューが無い（HID だけで書く） |

printf の系統（`Serial.printf`、`printf`、`sprintf`、`snprintf`）、`Serial.print(float)`、`String(float)` はコアの書式化で動きます。
既定（`fltf`）は AVR と同じく float で計算し、printf の `%f` も出します。小数を表示するスケッチは、double で計算するより約 2〜2.5 KB 小さくなります（V003、X035、V307）。
15 桁の精度が要るとき（GPS の緯度経度など）は `dbl` / `dblf` を選びます。小数を使わずに printf を使うときは、`%f` なしの `flt` で約 1 KB 小さくなります。FPU のない系列では、小数の表示に soft-float を使いません（float の計算を値のビットに対して行います）。
小数も printf も使わなければ、どの選択肢でも大きさは同じです。
`strftime()`、`gmtime_r()`、`localtime_r()`、`mktime()`、`tzset()` もコアのもので、出す文字は newlib 4.5 と同じです。
`Serial.parseFloat()` の計算は、parseFloat のメニューで、double（`double`：いちばん近い float）、整数（`int`：11 桁までいちばん近い float、double の計算なし）、float（`float`：いちばん小さいが、最後の 1 ビットがずれることがある）から選べます。
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
| `libraries/FS/src/fatfs/` | FatFs R0.16 と作者の patch 1・2 | FatFs の license（1 条項の BSD 相当） |
| `libraries/Network/src/lwip-src/` | lwIP 2.2.1 | BSD-3-Clause |
| `libraries/NetworkClientSecure/src/mbedtls-src/` | Mbed TLS 4.1.1 | Apache-2.0 または GPL-2.0-or-later |
| `libraries/CH32RVUsbHost/src/CH32RVUsbHostCdc.cpp` の USB シリアルチップの手順 | TinyUSB の cdc_host.c（Linux のドライバ由来）を書き直したもの | MIT |

固定 revision とファイル hash は `vendor/*.lock.toml` に記録し、検証ツールで一致を確認します。
