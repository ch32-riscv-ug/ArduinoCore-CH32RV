# tests

[テスト計画](TEST_PLAN.ja.md)と[カバレッジ](../docs/test-coverage.ja.md)に従い、通常の検査と、接続先を明示する実機契約を分けます。旧suiteと設定はimportしません。

## ボード不要の検査

```sh
cd tests
uv sync --locked
uv run --locked pytest
```

repository rootからは `uv run --project tests --locked pytest -c tests/pyproject.toml` です。通常の収集範囲は `unit/` と `platform/` で、Arduinoへの書込み、monitor、電源操作をしません。`platform/` は旧 `build/` の名前を変更したもので、固定sourceのオフライン整合検査を置きます。生成物のディレクトリではありません。

## 実機契約

試験の相手は、アドレス 1 つで名指します。ch32rv に渡すのと同じもので、`tests/.env` の `CH32RV_PROBE` に書きます（[.env.example](.env.example)）。OEP の口なら `oep://<unit>/<口>`、WCH-Link なら `wchlink://<serial>` です。USB で見つからないプローブ（CH340 の後ろの ESP32 など）は、経路を付けて `oep://<unit>/<口>?via=tty:<パス>` と書きます（UIAPduino の 0070070d9394）。設備のファイル（bench.toml）はありません。ほかの事実は、本当にある場所から読みます（[harness/place.py](harness/place.py)）。

| 事実 | どこから |
|---|---|
| プローブの USB の位置、電源を切る hub の口 | USB のシリアル（OEP の unit ID、Link のシリアル）で sysfs を引く |
| 線と DUT の端子 | プローブの設定のラベル（`<口>@<端子>`、`<口>.<役目>@<端子>`） |
| 型番、UID、チップ ID | チップ（`ch32rv target info`）。チップ ID で型番が決まらないときだけ、記録の `part` から候補の 1 つを選ぶ |
| 基板、外付けの部品、DUT の USB の先、電源、不良 | プローブのメモ欄の記録（下） |
| fqbn、クロック | boards.txt |
| 系列ごとの周辺の有無 | [coverage.toml](coverage.toml) の needs と variant の定義 |
| DUT の USB、相手の ESP32-S3 | 実行のときに USB のシリアルで探す（DUT は UID、S3 は記録の serial） |

流す試験は、これらから決まります。流さない試験は、理由といっしょに result.json の `not_chosen` に残ります。記録が別の個体を指していれば（UID が違う）、何もせずに止めます。

```sh
uv run --no-project python tools/index/fetch_tools.py --tool ch32rv
uv run --no-project python tools/index/fetch_tools.py --tool xpack-riscv-none-elf-gcc
uv sync --project tests --locked --extra hardware

# その口で何が流れるかを見る（チップは読むだけ）
uv run --project tests --locked --extra hardware --env-file tests/.env python tests/bench_record.py plan

# ビルドだけ／書込み、観測、試験
uv run --project tests --locked --extra hardware --env-file tests/.env python tests/run_hardware.py --case runtime
uv run --project tests --locked --extra hardware --env-file tests/.env python tests/run_hardware.py --execute

# 相手を並べる、case を選ぶ
uv run --project tests --locked --extra hardware python tests/run_hardware.py --execute \
  --probe oep://80f1b2d835d6/x035c8t6-pico --probe wchlink://A94B8F062EE9 --case gpio --out work/run1
```

`--probe` と `--out` は環境変数より優先します。`--out` を省略すると `OEP_HW_RESULTS` 内に実行ごとの新しいディレクトリを作ります。書込みには常に `--execute` が必要です。機器の排他は pytest の機器ロック（`--device-lock required`）が受け持ち、ベンチの共通ロックは取りません。`--case` で、その相手で流せない case を求めると、理由つきの SKIP になります。

新しいクロックの設定や休止など、接続できなくなるかもしれないイメージを試すときは、`CH32_TEST_BOOT_HOLD_MS=1000` を付けます。
その回のすべてのイメージが `-DCH32RV_BOOT_HOLD_MS=1000` 付きでビルドされます（docs/upload-routes.ja.md「接続できなくなる
イメージを試すとき」）。メニューの値は `CH32_TEST_OPTIONS`（例 `clock=48,opt=os`）で選びます（RECORDS.ja.md）。

### 取り込みの速さ

線を取り込んで WireSkein で判定する試験は、取り込みの速さを次の基準で決めます（2026-11-14、ユーザーと [oep] と決めた）。

- **基準の長さ T は、問いごとに決めます。** 見たいものの一番短い長さです。
  - 取引の復号（UART、SPI、I2C など）: 一番短い半周期か、ビットの幅。
  - 時間の判定（セットアップ、ホールド、遅れ、パルスの幅）: 測る量か、その許容の幅。
  - ひげの判定（`i2c` の `spike_max_s`、`spi` の SCK のグリッチなど）: 判定したい一番短いパルスの幅。
- **速さは 10 / T 以上を基本にします。** 最低は 5 / T で、20 / T あれば足ります。プローブの上限に当たらない限り、高めに
  取ってかまいません（分析は WireSkein がするので、取る側の負担はあまり増えない）。
- **1 つの区間に問いが複数あるときは、一番短い T で速さを決めます。** そこまで届かない問いは「この速さでは未検査」とし、
  試験の文書とコメントにそう書きます。
- **5 / T に届かない取り込みで、その判定を通ったことにしません。** 2 MS/s で 50 ns のひげを「無い」と判定していた見落とし
  （6-2 の wire、2026-11-14）がこの形でした。WireSkein（[wireskein] 2026-11-14 の版から）は、粗い取り込みでも照合して
  OK / NG を出し、T に 5 サンプル届かない所に注意 `under_rate` を付け、報告のどの行にも答えた長さと細かさを書きます
  （ユーザー: 分析を断るより回す）。届かないことを失敗にしたい判定は `notices={"under_rate": "ng"}` と書きます。
- **グリッチフィルタ（取り込みの `glitch_filter_ns`）を掛けた取り込みは、その幅より短いひげを消します。** そのときのひげの
  判定は、フィルタの幅以上の T でだけ意味があります。復号を安定させるためにフィルタを掛けるか、ひげを見るためにフィルタ
  なしで 5 / T で取るかは、問いごとに決めます。
- **プローブの上限**（本数ごとの流し続けの速さ、USB とホストで決まる送り出し）は oep-probe-arduino の
  docs/guide/capture.ja.md が正です。上限で届かないときは、取る本数を減らすか、その問いを未検査と明示します。

今の試験の当てはめ:

| 試験 | T | 速さ |
|---|---|---|
| wire | wire.output-only: 頼む SCL の一番短い半周期 147 ns（3.4 MHz）。wire.repeated-start は 20 kHz（半周期 25 µs）。`i2c` のひげの判定（50 ns）は 5 / T。WireSkein 0.0.28 は I2C を規格の入力どおりに読む（50 ns 未満を捨て、遅い縁の何度ものまたぎを 1 つに、線に写らなかったクロックを補う。注意 slow_edge・clock_restored）。グリッチフィルタもヒステリシスも掛けない | 100 MS/s（3.4 MHz で 14.7 / T） |
| wire.timeout、wire.clock-stretch、wire.bus-recovery、wire.10bit、wire.general-call、wire.pec、wire.controller-and-target（softwire も） | DUT が controller のものは 20 kHz（半周期 25 µs）、プローブの i2c-controller が相手のものは 100 kHz（半周期 5 µs）。`i2c` のひげの判定（50 ns）は wire と同じ | 100 MS/s（20 kHz で 2500 / T、100 kHz で 500 / T） |
| spi、softspi | SCK 1 MHz の半周期 500 ns。CS の 12.5 ns 以下のひげは、どの速さでも 5 / T に届かないので判定しない | 40 MS/s（20 / T） |
| spi.slave、spi.peripheral | SCK 100 kHz の半周期 5 µs | 40 MS/s（200 / T） |
| softspi.slave、softspi.peripheral | SCK 50 kHz の半周期 10 µs | 40 MS/s（400 / T） |
| softwire.slave | SCL 100 kHz の半周期 5 µs（wire.slave と同じ。`i2c` のひげの判定 50 ns は wire と同じ 5 / T） | 100 MS/s |
| uart | 1 Mbaud のビット 1 µs | 20 MS/s（20 / T） |
| uart.flow-control、uart.multiprocessor、uart.receive-event、software-serial | 115200 のビット 8.7 µs（CTS・RTS の変わり目は数十 µs で見る） | 4 MS/s（35 / T） |
| uart.rs485 | 1 Mbaud のビット 1 µs（DE の前後は 20 µs 以内を見るので十分細かい） | 20 MS/s（20 / T） |
| uart.half-duplex | 9600 のビット 104 µs（開放ドレインとプローブのプルアップ） | 1 MS/s（約 100 / T） |
| periph.hs-adc | 1 MHz・デューティ 0.25 の High 250 ns | 80 MS/s（20 / T） |
| periph（uart2、adc-stream）、signals、system | 115200 のビット 8.7 µs、Servo の幅の許容 30 µs、眠りの ms | 1〜5 MS/s（20 / T 以上） |
| signals.complementary | 周波数の区間は 20 kHz の 2 %（1 µs）、デッドタイムの区間はその許容 100 ns | 10 MS/s（10 / T）、デッドタイムは 100 MS/s（10 / T） |
| signals.one-pulse | 50 µs の幅の許容 1.4 µs（2 % + 0.4 µs） | 5 MS/s（7 / T） |
| signals.pwm-route、pwm-complementary、output-compare、ticker、hw-timer | duty の 1〜2 点（1 kHz で 10〜20 µs）、Ticker・alarm の幅の許容 20 µs | 1 MS/s（10 / T 以上） |
| signals.rgb-led | `rgbLedWrite()` の T0H 0.35 µs（WS2812B の窓 0.25〜0.55 µs） | 60 MS/s（17 / T、20 / T は 57 MS/s。3 本で 4 本ぶんの幅、プローブの上限の内） |

### 記録（プローブのメモ欄）

個体ごとに違い、機器から読めない事実は、プローブのメモ欄（oep-spec oep-if-memo §4）に、口ごとに置きます。プローブについて行くので、プローブを別のホストにつないでも、そのまま試験できます。プローブは中身を読まないので、守らせる必要のある値は置きません。

| 名前 | 持ち主 | 中身 |
|---|---|---|
| `oep.probe.memo/port/<口>` | OEP（§4.3） | `target`（`<系統>-<UID>`、照合に使う）、`part`（チップ ID で決まらないときの型番）、`note` |
| `io.github.ch32-riscv-ug.arduino/port/<口>` | このコア | `board`（`<作り手>:<型>`）、`supply`（`{from: probe \| usb \| other}`）、`usb`（1 つの口なら`{to: host}`、`{to: esp32-s3 \| esp32-p4, serial と mac の一方または両方}`、`{to: rp2040, port}`。コントローラーごとなら`{full_speed \| high_speed \| super_speed \| usbd \| high_speed_2: <同じ形>}`。キーはコアのコントローラーの選択の名前。同じ相手の口が複数あると、case は最初の口だけで流し、ほかは not_chosen に書く）、`parts`（`{sd: {bus, <信号>: <端子>}, eth: {to: lan}}`）、`pd`（`{max_mv, vbus_pin, vbus_ratio}`）、`defects`（`[{id, lose: [機能], why}]`）、`note`、`updated` |

名前、形、メンバーごとの決まり、読み方と書き方は [RECORDS.ja.md](RECORDS.ja.md) にあります（検査の実装は [harness/records.py](harness/records.py)）。書くときは `tests/bench_record.py set`（同じセッションの中で読んで書き、渡した項目だけを変え、ほかの道具の項目は残す）、見るときは `show` を使います。正しい中身はホストの側にも残してください。WCH-Link は記録を持てないので、記録の要る試験（sd、eth、usb の相手など）は流れません。ただし WCH-Link には、tests/.env の `OEP_PORT_MEMO_<serial>`（`wchlink://<serial>` と同じ serial）で同じ形の記録を渡せます（[RECORDS.ja.md](RECORDS.ja.md) §5）。プローブが運べない console（WCH-Link(CH549) の SDI など）は、`ch32rv capabilities` の答えで飛ばします。チップ ID で型番が決まらない Link の相手には、`CH32RV_CHIP` を書きます。

```sh
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://80f1b2d835d6/x035c8t6-pico \
  --target auto --board weact:CH32X035-CoreBoard-V1.0 --supply usb --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c
```

| case / contract | 必要設備と確認するもの | 確認しないもの |
|---|---|---|
| runtime / runtime.init、runtime.time-progress、runtime.print-heap、console.round-trip | DUTとDMSEQ対応プローブ。毎回のbuild ID＋challenge応答、data/bss/constructor、Print/String/malloc、時間APIの粗い進行 | OOM・断片化、時間精度、全clock条件・wrap境界、全console方式 |
| uart / uart.external-round-trip、uart.output-only、uart.formats、uart.receive-errors、uart.flow-control、uart.rs485、uart.half-duplex、uart.multiprocessor、uart.receive-event、uart-sync | uart.output-only: uart.formatsと同じ条件を、プローブのUARTを付けずにDUTのTXだけ取り込んでデコードする（切り分けの基準）。標準UARTに接続した独立UART。115200 8N1、64 byte binaryの往復とstdio。uart.formats（OEPの口、ラベル`<口>.rx@<端子>`と`<口>.tx@<端子>`）: 9600・115200・1 Mbaudの8N1と、115200の8E1・8O1・8N2・7E1・7O1を、2本の線の1本の取り込み（20 MS/s）で、条件ごとのWireSkeinの区間にする。DUTが送る16バイトを`uart`でデコード（中身、形式、F_CPU÷BRRのボーレート3 %以内、フレーミングとパリティの誤りなし）、プローブのUARTが送る16バイトも同じくデコードし、DUTが受けた数とFNV-1aを比べる。条件の始めと終わりは両方の線がidle、送っていない側の線は動かない。uart.receive-errors: プローブのUARTの形式をずらして誤りを起こし（8E1にした DUT へ 8O1、8N1 の DUT へ偶数パリティの 8E1 と 0x00、読まずに 100 バイト、割り込みを止めたまま 16 バイト）、`Serial.onReceiveError()` が報告した種類（パリティ・フレーム・ブレーク・バッファ満杯・オーバーラン、誤りのない条件では何も）と DUT が残したバイト数を比べる。送った波形はプローブの形式で WireSkein がデコードする。スケッチは sketch_support/uart/uart.ino（core_contracts から分けた。V003 の 16 KB に収めるため）。次の 4 つはプローブの素の線（ラベル`<口>@<端子>`、役割なし）を使い、足りない台は理由を書いてスキップする。uart.flow-control: Serial の CTS / RTS の端子を素の線から DUT に探させ（`setHwFlowCtrlMode()` が受けた端子）、CTS はプローブが High にして DUT が 1 バイト書いても TX が動かないこと、Low にすると 16 バイト出ること。RTS はしきい値 16 で DUT が 800 ms 読まない間にプローブが 32 バイト送り、17 バイト目からDUT が読むまで RTS が High のまま、読んだ後に Low に戻ること（WireSkein の `level`・`becomes`・`starts`・`ends`。保持の前は 1 バイトごとに割り込みが取り出すまで約 10 µs の RTS のパルスが出る。USART の RTS はデータレジスタが埋まっている間 High のため）、DUT が 16 保持して最初の 17 を読めること（残りはオーバーラン）。uart.rs485: 素の線の 1 本を DE にして（`setMode(UART_MODE_RS485_HALF_DUPLEX)`）、115200 と 1 Mbaud で 4 バイト、2 ms あけて 12 バイト。WireSkein の `rs485` で、どの文字も DE が High の窓の中、DE は最初のスタートビットの 20 µs 以内前に上がり、最後のストップビットの後 20 µs 以内に下がる（途中で下げない）、バイトの順、区間の始めと終わりは DE が Low。uart.half-duplex: DUT の TX の端子だけで 9600 の 1 線（`setPins(tx, tx)`）、プローブはその線を入力プルアップ。DUT が送る 16 バイトを線でデコードし、DUT の受信バッファには何も戻らないこと（USART は送信中に受信しない。ESP32 の one-wire と違う。2026-11-17 に全系列で確認）。開放ドレインのある石では、続けてプローブの UART の TX をその線に付けて 16 バイト送り、DUT が受けた数と FNV-1a（X033/X035 は端子に開放ドレインが無く出力がぶつかるので飛ばす）。uart.receive-event: `onReceive()`。onlyOnTimeout で `setRxTimeout(1)`（IDLE）と 10 に、プローブが 5 バイトを 30 ms おきに 3 回: 呼ばれるのは 3 回で各 5 バイト。`setRxFIFOFull(8)`・タイムアウトなしに 24 バイト 1 回: どの呼び出しも 8 以上、残りは 8 未満、欠けなし。DUT の RX の線を WireSkein でデコード（1 回の送りの中に隙間があると別のタイムアウトになるため）。uart.multiprocessor: `setAddress()`・`writeAddress()`・`mute()`（115200、8 データビットで 9 ビットの語、9 ビット目がアドレスの印）。バイトもアドレス（3、5）も 1 の数を偶数にし、同じ区間を WireSkein の `uart` で 8E1 と 8O1 の 2 通りに読む（8E1 ではデータが正しくアドレスがパリティ誤り、8O1 では逆。`uart` に 9 ビットのデータが無いため）。プローブの UART はアドレスを 8O1、データを 8E1 で送って印を作る。sends: DUT が `writeAddress(5)` と 8 バイト。addressed: アドレス 3 + A、5 + B、3 + C で DUT が読むのは B だけ（アドレスのフレームも読まない）。mute: 5 + A、`mute()`、アドレスなしの D、5 + B で A と B だけ。idle: `setAddress(-1)`（8N1）で 1 バイト読ませ、B + C を続けて送る途中（B の最初のバイトを読んだ直後）に `mute()`、間をあけて D: 読むのは B の最初のバイトと D だけ（静かな線で `mute()` を呼んでもすぐ起きる。2026-11-19 に 5 台で確認）。uart-sync（別の case、スケッチは sketch_support/uart_sync/uart_sync.ino。V003 の 16 KB のため）: `UsartSPI`（Serial の USART の同期モード）をプローブの spi-target のコントローラーにする。CK は route の CK の端子（ポートのラベルのある線でなければスキップ）、TX が MOSI、RX が MISO、CS は CK でない最初の素の線。spi.modes と同じ条件（4 つのモードと両方のビット順を 250 kHz、モード 0 の MSB 先を 1 MHz）を WireSkein の `spi` で判定し、DUT が読んだ値と、DUT の答えのクロックが指定以下であることをここで比べる（40 MS/s）。同期モードの無い系列（V00x）は USYNC NONE でスキップ | ほかのinstance/route、5・6ビットと1.5ストップビット、3 Mbaudの端、ノイズ（NE）の再現、RTS で相手を止めること（プローブの UART に CTS が無い）、トランシーバーを挟んだ RS485（`rs485_*` のラベル）、SoftwareSerial の 115200 より速い受信 |
| software-serial | 素の線 2 本（ラベル`<口>@<端子>`、役割なし）の SoftwareSerial（RX、TX）とプローブの UART で、9600・57600・115200 の 8N1 と 9600 の 8E1・8O1・8N2・7E1 を両方向。DUT の送りは公称の速さの 3 % 以内で形式どおり、受けた数と FNV-1a、overflow なし（受信は 9600 まで必須、それより速い受信は記録）。スケッチは sketch_support/software_serial/software_serial.ino（2026-11-19 に uart から分けた。uart.ino を V003 の 16 KB に収めるため） | 115200 より速い受信 |
| pioc / pioc.pulses | PIOC の IO0 の端子のうち、プローブが素の線（`<口>@<端子>`）にしている最初のもの（無ければスキップ）。CH32RVPioc で sketch_support/pioc/bench_pulses.S を読み込み、n = 1・3・8 のコマンドごとに IO0 のパルスが n 個（WireSkein の `pulses`、区間の始めと終わりは Low）、cmd.out が 0x5A、cmd.out を受け取った後に要求が残っていない（`done()` が false）、d1 がコマンドの数。続けて `onDone()`（割込み）で 1 回だけ 0x5A で呼ばれる。パルスの幅は PIOC のクロックで決まる（d0 = 40 で約 1.2〜2.5 µs）ので記録だけ。4 MS/s。2026-11-19 に H417（PA0）・X035（PC7）・V205（PB10）で通る | IO1、入力（WAITB の端子のイベント） |
| uart-dma / uart.dma-receive、uart.dma-interrupts-off、uart.dma-transmit、uart.dma-receive-event | uart.formats と同じ線（ラベル`<口>.rx@<端子>`と`<口>.tx@<端子>`）とプローブの UART。スケッチは sketch_support/uart_dma/uart_dma.ino。どの試験も初めに受信 512・送信 256 バイトのバッファ（`setRxBufferSize()`・`setTxBufferSize()`、V003 の RAM に収まる大きさ）で begin し、USART の CTLR3 の DMAR / DMAT が立っていること（DMA を使っていて、組み込みの経路に戻っていない）を確かめる。uart.dma-receive: 1 Mbaud と 2 Mbaud（DUT の USART が届く速さ、`dmax` の答え）で 4096 バイトを受け、数と FNV-1a、誤りなし。DUT の RX を WireSkein の `uart` でデコード（20 MS/s、2 Mbaud で 10 / T。40 MS/s はプローブの流しがあふれた）。uart.dma-interrupts-off: 1 Mbaud で DUT が割り込みを 1 秒止めている間に 400 バイト。割り込みを止めたままの `available()`（DMA の数）が 400、後で全部読めて誤りなし。640 バイト（バッファより 4 分の 1 多い）では UART_BUFFER_FULL_ERROR だけが出て新しい 511 バイトが残る。uart.dma-transmit: 1・2 Mbaud で 2048 バイト（バッファの 8 倍）を書き、プローブの UART と WireSkein（DUT の TX、20 MS/s）が全部受け、最初の write から flush() までが、線の時間と DUT が測った write() 2048 回の時間の長い方の 5 % と 1 ms 以内（DMA が待ちを足さないこと。write() 1 回の時間は DMA あり・なしとも記録）。割り込みを止めて書いても全部出る。uart.dma-receive-event: DMA の受信バッファで uart.receive-event と同じ条件 | CH32RVAdcStream・CH32RVLogicCapture と同じチャネルを取り合うとき（組み込みの経路に戻ること）、RTS と DMA 受信、2 Mbaud より速い速度、バッファ 1 周より多いあふれ（見えないことがある。HardwareSerialDmaRx.cpp） |
| gpio / gpio.external-levels | プローブのラベル`<口>@<端子>`（と、部品のつながらない役割の線）の全部。出力の外部観測と外部入力刺激、終了時INPUT。ラベルが無ければスキップ | 未配線pad、全package、analog/PWM/interrupt |
| signals / signals.pwm、signals.servo、signals.tone、signals.interrupt、signals.timer-input、signals.capture-after-pwm、signals.rgb-led | プローブのラベル`<口>@<端子>`の線から1本（PWMとタイマーの入力は、DUTがvariantの表でタイマーの端子だと答えた線）と、隣の線2本。試験ごとに1本で続けて取り込み（`instrumented/ws_capture.py`のstream）、条件ごとにWireSkeinの区間`## <条件>`を開いて命令を送り、その区間に線がなる状態を`becomes`で書く（前の状態から1度だけ変わり、変化のあとはどの周期も範囲内）。外側の区間は`only_moving`で、隣の線が動かないこと。区間はプローブの時計で1本の取り込みから切り出され、どの条件で外れたかは1つの記録で分かる。判定はWireSkein（pytest-embedded-wireskein）: analogWriteの64・128・192で1 kHz、同じ値の2回目、255と0でHighとLow、128の2 kHzと500 Hz、止めた後のLow。Servoの544〜2400 µsと2400の2回目で50 Hz・その幅（30 µs）、detach後のLow。toneの440 Hz・2 kHz・2 kHzの2回目で50 %、noTone後のLow、1 kHz・30 msで30周期の後にLow。どの波形の条件も、変化のあとに20周期以上と、命令から50 ms以内の変化を求める。周波数は2 %。切り替えで切れてよい周期の数はコアのきまり: デューティとServoの幅は0、PWMの周波数は2、toneは1、出し始めと止めるとき（analogWriteの0と255を含む）は1。割り込み（RISING・FALLING・CHANGEの数）とCH32RVPulseCapture（プローブのPWMの周期とHighの時間）はDUTの答えを比べる。analogWriteを止めたあと（0、pinMode、digitalWrite）に同じ端子でCH32RVPulseCaptureが始められること（動いている間は断られること）も確かめる。signals.rgb-led: `rgbLedWrite()`のLED 1個、もう1回、`ch32rv_rgb_led_write()`で3個を1つのフレームで、を60 MS/sで取り込み、WireSkeinの`rgb_led`がバイト（GRB）と、T0H・T1H・ビットの周期がWS2812Bの窓（0.25〜0.55、0.65〜0.95、0.65〜1.85 µs）に入ること、フレームのあとのLowが50 µs以上を判定する。試験ごとにDUTを再起動する | 2本以上の同時の出力、高い周波数、Servoの複数、入力に戻した後の線（プルが無いと浮くので見分けられない） |
| system / system.reset-reasons、system.watchdog、system.sleep、system.window-watchdog、system.backup-registers、system.rtc-alarm | system.reset-reasons: `CH32RV.restart()` の後の`resetReason()`がSOFTWARE、プローブがNRSTを引いた後がEXTERNAL。system.watchdog と system.sleep（試験の関数を分け、取り込みもそれぞれ。眠れない系列は`CH32RVSleepResult::Unsupported`の答えで sleep を n/a のスキップ）: ラベル`<口>@<端子>`の線を2本（1本目は計時の線で、DUTが手放すとプローブの入力プルダウンでLow、2本目はプローブが動かす起こす線）、1本の取り込み（1 MS/s）に条件ごとのWireSkeinの区間。計時の線は計る間だけHigh: ウォッチドッグの最後の餌からリセットまで、眠っている間。そのHighの長さはWireSkeinの分析（states）から読み、ここで条件の範囲と比べて記録に書き戻す（`ws_run.result`）。ウォッチドッグ: `wdtEnable(250 / 1000)`を50 msごとに600 ms餌をやってリセットされないこと、そのあと餌を止めてHighの長さがタイムアウト（X033 / X035はHSI÷1024で -7 %〜+3 %、ほかはLSIで -60 %〜+70 %）、次の起動がWATCHDOG。スリープ: タイマー200 msの`lightSleep()`（OK、TIMER、`millis()`の進みがHighの長さの20 %以内、Highが0.1〜0.3 s）、起こす線をLowにして起きる`lightSleep()`（PIN）、タイマー200 msの`deepSleep()`（次の起動のwakeupCause()がTIMER）。窓ウォッチドッグ（同じ2本と取り込み）: T = min(`wwdgMaxTimeoutUs()`, 40 ms)、窓 T/2 で300 ms窓の中で餌、そのあと餌なし（Highの長さがTの1カウント以内、早期警告のコールバックが1回）と、最後の餌からT/8で窓の前の餌（HighがT/8、コールバック0回）。どちらも次の起動がWINDOW_WATCHDOG。バックアップの語（線なし）: 全語に模様を書き`restart()`をまたいで全部読めること（数が0の系列はn/a）。RTCのアラーム（同じ2本と取り込み）: `setAlarm(time() + 2)`のコールバックが線を下げるまで1〜3 s、getAlarm()が0に戻る、`enableAlarmWakeup()`の`lightSleep()`がALARMで同じ長さ、`deepSleep()`の次の起動がALARM（RTCの無い系列はn/a） | POWERONの要因、スタンバイの系列（V20x / V30x）のdeepSleep、ウォッチドッグを付けたままの眠り、4本の端子の起こし、電源断をまたぐBKP |
| console-rtt / console-dmdata / console-sdi | デバッグのコンソールをスケッチのただ1つのコンソールにする（`sketch_support/console`、ビルドのflag `CONSOLE_RTT` / `CONSOLE_DMDATA` / `CONSOLE_SDI`、モニターの`source`はケースのport_config）。RTTとDMDATAは双方向: nonceにビルドIDで答える、ランダムな文字列20行のecho（80文字まで）、番号とハッシュ付きの200行がそろって順に届く（RTTは`availableForWrite()`を見て待つ。`write()`は入らない分を捨てる仕様）。SDIは送るだけ: 100 msごとの行がビルドIDと番号を持ち、21行で番号が飛ばない。WCH-Link以外ではスキップ | SDIはV307のLinkEで届かない（DMDATAは同じ番号で通る、[ch32rv]に調査を依頼、2026-11-05）、RTTのバッファの大きさの変更、ホストがいないときの挙動 |
| analog / analog.dac | 配線なし。variantがDACの出力と名指す端子（`CH32RV_DAC1_PIN` / `CH32RV_DAC2_PIN`）ごとに、`analogWrite()`を既定の8ビットと`analogWriteResolution(12)`で満量程度の0・1/4・1/2・3/4・全部に書き、同じ端子を`analogRead()`で8回読む: 平均が書いた値からADCの満量程度の4 %以内、8回の幅が1 %以内。DACの無い部品はスキップ | 外部の電圧計での絶対値、負荷をつないだとき、DACの更新の速さ |
| hid / upload.hid-runtime | UIAPduino V003、ソフトUSB、OEP SWIO＋NRST。HID権限、Arduino upload recipe、独立DMSEQ上の新build IDとruntime | Arduino USB stack、USB PD、bootloader領域の書換え |
| power / usb.port-cycle | 指定PPPSハブ。対象だけのUSB消失、再列挙、個体照合、DUT再attach | VDDが0 Vまで落ちること、DUTのcold boot証明 |
| usb / usb.enumerate、usb.cdc-round-trip、usb.hid-composite、usb.hid-keyboard、usb.midi-round-trip、usb.net-frames、usb.reconnect、usb.isp-entry-dfu-detach、usb.isp-entry-touch | DUT自身のUSB端子（`dut_usb_topology`）をホスト役につなぐ。CH32RVUsbDevice の複合デバイス（CDC、キーボード＋ベンダーHID、MIDI、既定のDFUランタイム）の列挙とこのbuildの照合、CDC 64 byte binary、HIDのレポートID・ベンダー往復・キー入力、MIDI往復、NCMのMACどおりのインターフェースと双方向のフレーム（root不要）、`end()`→`begin()` での離脱と再列挙、DFU_DETACH と1200bpsタッチでのISP（1a86:55e0）移行と自動復帰 | 大量転送の速度、サスペンド・レジューム、VBUSに依存する動作、ISPでの書き込み、X035以外の系列 |
| usb-classes / usb.webusb-descriptors、usb.ms-os-20、usb.vendor-round-trip、usb.vendor-request、usb.msc-block、usb.msc-file、usb.ccid-apdu | usb と同じ配線。X035のエンドポイント数でusbと同居できないクラス（ベンダーのバルク、MSC、CCID）の複合デバイス。BOSのWebUSB・MS OS 2.0（WinUSBの互換IDとGUID）とWebUSBのURL、ベンダーのバルク往復とベンダーリクエスト、MSCのINQUIRY・容量・ブロックの書き込みと読み戻し、ホストが置いたFAT上のファイルをスケッチが読めること、CCIDの電源投入（ATR）とAPDU1往復 | Windows／ChromeでのWinUSB・WebUSBの実動作、PC/SC経由のCCID、MSCの速度、OSのFATドライバでのマウント |
| eth / eth.dhcp、eth.icmp、eth.udp-echo、eth.tcp-echo、eth.tcp-both-ways、eth.http-get、eth.https-verified、eth.https-wrong-ca | ETH のある系列（H417 と V203RB で実機確認。V203RB は CPU Clock が 120 / 60 MHz のときだけ ETH が動く。V407 / V467、V307、V317 はビルドまで。V208 はベンチに置かない）の RJ45 をこのPCと同じLAN（DHCPあり）へ。DHCP、ping、UDP／TCPの往復の中身、両方向4 MiBの中身と量、HTTPのGETの本文のCRC、HTTPS（試験ごとにopensslで作るCAでECDSA／RSAのサーバーを検証、setInsecure、別のCAのサーバーは断られること）、試験中にPHYのリセットがないこと。速さはjunitのpropertyに残す。TLSの無い系列（V208／V203RB）はHTTPSの段を飛ばし、ヒープが足りない（-0x7F00、V307／V317）ときは記録して飛ばす | 10Mのリンク、リンクの抜き差し、長時間の連続、IPv6、H417・V203RB 以外の実機 |
| selfcheck / selfcheck.clocks、serial_transmit、serial_receive、spi、gpio、pwm、vref | 配線なし（LinkEのDUTでも流せる）。ビルドが前提とするバスのクロック（`CH32RV_PCLK2_HZ`）がRCC_CFGR0のPPRE2と一致。HardwareSerialの全部で、BRRが自分のバス（USART1はPCLK2、ほかはPCLK1）のクロック÷速度で、64バイトを送る時間が115200と1000000で1バイト10ビットの4 %以内。受信は、RXの端子をGPIOの出力にしてスケッチが9600で16バイト（先に捨てる0xFF）を打ち、全部が順に誤りなく届く。既定のSPIのBRと500 kHzでの時間。gpioは、その石の全部の端子から、device-dataの端子表のデバッグ・NRST・BOOT・水晶・USB（D+/D-、CC）を除き、プルアップ1・プルダウン0を読んでから（変わらない端子は外から押さえられているので駆動しない）、出力の読み返しと、出力にした端子で起こすRISING 5・FALLING 5・CHANGE 10回の割り込み。PWMは、タイマーのチャネルのある端子（除く端子・押さえられた端子・DACの端子を除く）で、`analogWrite()`の64と192を1 kHzと10 kHzで出し、同じ端子を50 ms読んで、周期の数が2 %以内、1の割合がdutyの4 %以内。`getVddMillivolts()`が2.7〜5.5 V。記録のdefectsが`gpio-drive`を失う基板（M030のモーター基板）では、端子を駆動する確認を飛ばす | どの端子に出るか、レベル、相手との実際の通信（配線のある試験で、系列・周辺ごとに代表1つ） |
| selfbus / selfbus.spi_out、spi_in、uart_formats、uart_errors、uart_transmit、i2c_registers、i2c_nack | 配線なし（selfcheckの続き。V003の16 KBに収まらないので別のスケッチ）。既定のSPIで0xA5と0x3Cを4つのモード・MSB／LSBで一番遅いクロックで送り、スケッチがSCKとMOSIを自分の端子の入力レジスタで読み続ける: SCKの待機レベルがCPOL、縁が16回、モードの読み取り側の縁で組み立てたMOSIのバイトが送ったバイト。MISOをGPIOの出力にして0／1を出し、受信が0x00／0xFF。除く端子・押さえられた端子があれば飛ばす。UARTは、RXの端子が空いている全部のポートで、8N1・8E1・8O1・7E1・8N2の8フレームが誤りなく届くこと、パリティ反転（PE）・ストップビット0（FE）・全部0（ブレーク）で捨てられて記録されること、割り込みを止めた3フレームでORE、読まずにバッファより多くでFULL。送信は各形式で1フレームのビット数どおりの時間（4 %以内）。I2Cは、`setClock()`の100 kHz／400 kHzの後でFREQがPCLK1のMHz（上限を超える系列は上限）、CKCFGRが標準・高速の式どおり。相手のいない送信が200 ms以内にエラーで返る（両方の線がプルアップされていれば2）。`gpio-drive`を失う基板（M030）は飛ばす | 実際の相手とのSPI・UART・I2C、SCKの最高速、DMA |
| selfsys / selfsys.identity、temperature、restart、watchdog、window_watchdog、backup_registers、tone、servo、adc_pads、rtc、rtc_alarm、light_sleep | 配線なし。`getUniqueId()`がch32rvの読んだUIDと一致、温度センサーが0〜70 ℃、`restart()`の後の理由がsoftware、ウォッチドッグ（500 ms）で0.15〜1.5秒後にリセットされ理由がwatchdog。窓ウォッチドッグで餌なしと窓の前の餌のどちらも理由がwindow_watchdog、早期警告のコールバックが餌なしで1回・早い餌で0回。バックアップの語が`restart()`をまたいで残る（数が0ならn/a）。空いた端子で`tone(1000)`が100 msに100周期、Servoの1000／1500／2000 µsを`pulseIn()`で3 %以内。アナログの全部の端子を出力の1／0にして、直前に別のチャネルを変換してから読み、1で80 %以上（H41xのVIO18の端子は30 %）、0で10 %以下（記録のdefectsが失うチャネルは判定しない）。RTCの系列で`gettimeofday()`が3秒で10 %以内、`setAlarm(time() + 2)`のコールバックが0.95〜3.1秒後、アラームで起きる`lightSleep()`がALARM（RTCが無ければn/a）。`lightSleep()`がタイマーの200 msで戻る（最後に流す。戻らないとコンソールも戻らない）。deepSleepはsystemの試験に任せる | 実際の電圧での精度、消費電流、ピンでの復帰 |
| selfapi / selfapi.crc、rng、millivolts、priorities、cores-hsem、cores-ipc、cores-v3f-serial | 配線なし（selfcheck・selfsysの続き。V003の16 KBにどちらも余地が無いので別のスケッチ）。`CH32RV.crc32()` / `ch32rv_crc32()`がCRC-32/MPEG-2の既知の値（"123456789"で0x0376E6E7、EVTのCRC_Calculationの1〜128のバイトで0x199AC3CA）、同じ128バイトを1・2・3・5・7…に分けても同じ、番地のずれた126バイトがPythonで計算した値（CRCの回路のある系列はハードウェア、ほかはソフト）。`ch32rv_random()`の64語と`ch32rv_fill_random()`の7バイト×8で、同じ語が無い、0とFFFFFFFFが無い、2048ビットの1が50 %±5 %、各ビットの位置が64語のうち12〜52回1（統計の目安だけ。乱数の質は測らない）。ADC1の端子（除く・押さえられたものを除き4本まで、H41xのVIO18の端子は除く）を出力の1／0にして`analogReadMilliVolts()`の換算（`ch32rv_adc_millivolts()`、端子をそのまま変換）が1で電源の80〜110 %＋50 mV、0で10 %以下（電源は`getVddMillivolts()`、0なら3300 mV）。タイマー2本の更新割り込み（遅いほうは2 msごとに300 µsの処理、速いほうは100 µsごと）を、遅いほうを1レベル下げて50 ms（80 %以上が入れ子）、同じレベル（入れ子0）、遅いほうを上げて（入れ子0）。`gpio-drive`を失う基板（M030）はmillivoltsを飛ばす。H41x（CPU Core = V5F、1 つのイメージに両方のコアのコードが入り、`CH32RV.startV3F()`で V3F に関数を渡す）は、HSEM（V5F が持つ間 V3F は取れず離せない、離せば取れる、持つ側は取れない、`hsemTake(20 ms)`は 20〜40 ms で諦める、両方のコアが 2 万回ずつ同じ語を足して失われない）、IPC（1000 往復が全部届き、V3F の処理は mcause 0x80000011・コア 0、V5F は 0x80000010・コア 1、1 往復 50 µs 以下、`ipcReceive()`で待つ 1 通）、V3F で始めた USART（IALLOCR が V3F、64 バイトの送信が 115200 の時間の 4 %＋200 µs 以内、自分の RX の端子から送った 16 バイトを誤り無く受ける）。CPU Core = V3F では HSEM を 1 つのコアで、IPC は false を確かめる | mVの精度（外部の電圧源でのマニュアル試験）、乱数の質、3レベル以上の入れ子、VTF |
| selfexti / selfexti.software_trigger、rtc_alarm_line、wake、pvd | 配線なし（selfsys・selfapiの続き）。DUTが`CH32RV_EXTI_LINE_MASK`で挙げるピンでないEXTIの線のすべてを`attachExtiLine()`し、`triggerExtiLine()`（SWIEVR）でコールバックがちょうど1回、200 µs以内、`detachExtiLine()`の後は0回。RTCの系列で`attachExtiLine(CH32RV_EXTI_RTC_ALARM)`と`setAlarm(time() + 2)`の両方のコールバックが1回（線のほうは0.95〜3.1秒後）、線を外しても次の`setAlarm()`は鳴る。RTCの線を`enableExtiLineWakeup()`にした`lightSleep()`がEXTI_LINEで0.9〜3.2秒で起きる（休止するのでBOOT_HOLD。RTCが無い系列とV103はn/a）。`setLowVoltageDetect()`の最も高い・低い閾値がVDD（`getVddMillivolts()`）より250 mV以上上ならコールバック1回と`isLowVoltage()`、下なら0回（250 mV以内は記録だけ。PVDの無い系列はn/a） | VDDが本当に下がること（可変電源の単発）、USBなどの本物の起床の線、AWUの系列の線での起床 |
| logic / logic.continuous、logic.burst | DUTだけ（配線なし）。スケッチが取り込み用とは別のタイマーのPWMを自分のピンに出し（DACの端子は除く。設定の`logic_pin`で指定もできる）、そのポートをCH32RVLogicCaptureで取り込む。1 MS/sと10 kHz（RAMの小さい系列は250 kS/sと2.5 kHz）で周期がちょうど100サンプル、取りこぼしなし、captureBurstの周期がそろうこと | 外部の信号、16本すべての入力、24本のポートの上位8本、USBで流しながら |
| pd / pd.attach、pd.caps、pd.fixed、pd.pps、pd.restart | DUTのUSB-CにUSB PDの充電器。5 Vの契約が5秒以内にできること、profile 0が固定5 V、設定の`pd_max_mv`（既定5000）までの固定の電圧と5 Vへの戻り、どこにもない電圧が断られて契約がそのままなこと、PPS（あれば）の下限と中間、中間を（`maintain()`を呼ばず）system serviceだけで12秒保つこと、契約中の`end()`／`begin()`のあとに契約が戻ること。`pd_vbus_pin`と`pd_vbus_ratio`（分圧の比）があればVBUSを測って10 %以内 | ソース（`USBPDSource`）、Hard Reset、PD 2.0だけの充電器、ケーブルの向きの両方、M030のUSBPD1 |
| synckit / synckit.queue、synckit.notify、synckit.ring、synckit.critical、taskkit.executor | DUTだけ（配線なし）。タイマーの更新割り込み10 kHzがCH32RVSyncKitのQueue・Notify・Ringへ入れ、64 bitの値（上下の半分が常に等しい）を進める間、loop()が3秒取り出して確かめる。断られた分以外に欠け・重複・順番の乱れがないこと、CriticalSectionで読んだ64 bitが割れないこと、その区間が50 µs未満。守らずに読んだ値の割れは対照として記録。taskkit.executor: 同じ10 kHzの割り込みがpostするWork、10 msのPeriodで起きるWork、50 msのDeadlineで起きるWorkを1 sの間loop()のrunOnce()で回す。postはまとまる（走る数はpostの数以下で1以上）、最後の走りはすべてのpostの後、Periodの走りと取りこぼしの和が100、Deadlineの仕事は1回で遅れ5 ms以内 | 複数のISR、優先度の違う割り込みの入れ子、H417の2コア、LoopOnlyの誤用の検出 |
| flash / flash.units、flash.kept、flash.time | DUTだけ（配線なし）。`ch32rv_flash.h` の5つの関数を末尾の作業用の領域で試し（消す、書く、消して書く、2単位の消去、境界にそろっていないデータからの書き込み）、プログラム自身・単位にそろわない番地・フラッシュの外・長さ0を断ること。乱数の値を保存してソフトのリセットのあとに`setup()`が読めること。1回の`rewrite_page()`の時間と、その間の`millis()`・`micros()`の進みを記録。1回の実行で末尾の消去が約16回 | 電源断、寿命、書き込みの経路で保存が残るか |
| wire / wire.output-only、wire.repeated-start、wire.slave、wire.timeout、wire.clock-stretch、wire.bus-recovery、wire.10bit、wire.general-call、wire.pec、wire.controller-and-target、wire.data-nack、wire.arbitration | プローブのラベル`<口>@<端子>`の線で、DUTが答えたWireの既定のSDA / SCL（どちらもラベルがあること）。試験ごとに1本の取り込み（20 MS/s、「取り込みの速さ」）に条件ごとのWireSkeinの区間を置き、`i2c`が判定する。区間の始めと終わりは両方の線がHigh。問いは2つに分ける（2026-11-14）。クロックの問い wire.output-only: 相手なしで、プローブの端子の入力プルアップだけ。20 kHz・50 kHz・100 kHz・400 kHz・1 MHz・3.4 MHzのそれぞれで2つのアドレスに1バイト書き、線の上はS、ACKの無いアドレス、P、`endTransmission()`は2（400 kHzまで。RMのI2Cの標準・高速モード。それより上は設定はできるが動くかは石と線しだいなので、結果を記録するだけ。2026-11-15 ユーザー）。SCLの周波数はWireSkeinが測り、ここで判定する: 頼んだより速くない（3 %まで。コアの約束）、その台の一番速いSCL（上限、ここで測る）の5分の1以下の速さでは頼んだ値の10 %以内（設定が正しい）。線で遅くなる分は約束しない。取引の問い wire.repeated-start: プローブのi2c-target（0x42、内部プルアップ）を相手に、ベンチの遅い線が落ち着く20 kHzで、0x10を`endTransmission(false)`で書き、`requestFrom(addr, n, stop)`でn（1、2、3。読みの3つの経路）バイト読み、0x20を`endTransmission(true)`で書く。トランザクションごとの始まりと終わり（stop 0でS W Sr R Sr W P、stop 1でS W Sr R P S W P）、バイトとACK、バスの解放。DUTの戻り値（0、0）と読んだバイトは試験の側で比べる。fqbnでそのI2CをSoftware（`pin_i2c1=software`）にすれば、メニューのSoftwareのWireを確かめる。wire.slave: `Wire.begin(0x42)`をプローブのi2c-controllerが100 kHzで動かす。3バイトの書き込みはSTOPのあと`onReceive(3)`、`onRequest()`の4バイトの読み、1バイト書いてrepeated STARTで2バイト読むと`onReceive`が先で`onRequest`が後、用意より多く読むと残りは0xFF、40バイトの書き込みはバッファの32バイト（頭）、別の番地は応答なし。バスはWireSkeinの`i2c`、コールバックが見たものとプローブが読んだものはここで比べる。2026-11-17 に追加（ベンチ未確認、同じ取り込みと区間の形）: wire.timeout はプローブの gpio で SCL を握り（5 とタイムアウトのフラグ、離せば 2）、i2c-target の stretch を DUT のタイムアウト（`setWireTimeout`）の 3 倍にして 5、止めれば 2 回以内に 0。wire.clock-stretch は i2c-target が 1 バイトごとに 500 µs stretch して 3 バイトの書き込みと 2 バイトの読み。wire.bus-recovery は DUT が自分の端子で読みを 3 ビット目で止め（相手の 0x00 で SDA が Low）、次の転送は失敗してよく、その次は 0（自動の bus clear）。wire.10bit は controller の 0x2A5（ヘッダー 0x7A + W の NACK まで。i2c-target は予約アドレスを断る）と target の 0x2A5（プローブの i2c-controller から書き込み・読み・別の 10 bit・7 bit の 0x42）。wire.general-call は controller からアドレス 0、target で setGeneralCall の on / off。wire.pec は書き込みの PEC と read word の正しい / 違う PEC。wire.controller-and-target は target のまま controller として書き、続けてプローブと同時に書き合う（DUT は 2 だけ、プローブは done・アドレスの NACK・負け。softwire は n/a）。wire.data-nack と wire.arbitration はスキップ（プローブの i2c-target はどのバイトも ACK する、プローブの controller の START を DUT の START にそろえられない） | アービトレーションにわざと負けること、データの NACK（プローブの機能待ち）、controller の 10 bit を ACK する相手 |
| softwire / softwire.output-only、softwire.repeated-start、softwire.slave、softwire.timeout、softwire.clock-stretch、softwire.bus-recovery、softwire.10bit、softwire.general-call、softwire.pec | wireと同じ3つの試験を、Wireの端子でない`<口>@<端子>`の線2本のSoftWireで流す（ハードウェアのrouteでなくてよい。SCLはSDAと別のビット番号の線を選ぶ）。softwire.slaveはSoftWireの`begin(0x42)`（targetはSTARTの割り込みからSTOPまで線を見て追い、callbackの間SCLを握る）。2026-11-17 から wire の追加の試験も同じ線の SoftWire で流す（ベンチ未確認。softwire.controller-and-target は n/a: SoftWire の役割は 1 つずつ） | wireと同じ。STARTの割り込みの遅れと、各Lowで1回の書き込みが立ち上がりに間に合うこと（速いクロック） |
| sd / sd.spi、sd.sdio | ベンチのSDカード（FATのボリュームのあるもの）を、SDライブラリ（ラベル`spisd_*`）とSD_MMC（ラベル`sdio_*`、`sdio_d1`〜`d3`があれば4本）で識別・マウントし、64 KBを書いて読み戻し、ルートを一覧して消す。書きと読みの時間を記録。ラベルが無い方はスキップ | カードの種類ごとの違い、抜き差し、満杯 |
| spi / spi.output-only、spi.modes、spi.slave、spi.peripheral、softspi / softspi.output-only、softspi.modes、softspi.slave、softspi.peripheral | spi.output-only: spi-targetを付けず、プローブはCS・SCK・MOSIを取り込むだけ（MISOは浮くので判定しない）。下と同じ条件・同じ判定で、CSがHighの間のSCKは、待機のレベル（CPOL）が変わるときの1回の移りだけ許す。不具合がDUTか相手かを切り分ける基準。spi.modes: プローブのラベル`<口>@<端子>`の線: ハードウェアのSPIはDUTが答えた既定のSCK / MISO / MOSI（どれもラベルがあること）と残りの線のCS、SoftSPIは4本。OEPのspi-targetにつなぐ。モード0〜3とMSBFIRST / LSBFIRST（プローブが対応すれば）を250 kHzで、モード0のMSBFIRSTを1 MHzで、`transfer()`の3バイトと`transfer16()`の1語を送る。4本を1本の取り込み（20 MS/s。2026-11-19 まで 40 MS/s、負荷の高いホストで読み出しが追いつかなかった）にし、条件ごとのWireSkeinの区間で`spi`が判定する: 線から読んだモード、CSがLowの間に40クロック、SCKのグリッチなし、MOSIのバイトと語（MSBFIRSTで上位から、LSBFIRSTで下位から）、MISOの用意したバイト、区間の始めと終わりにCSがHigh。DUTが読んだ値はDUTの答えと比べる。CSがHighの間のSCKの動き（WireSkeinの注意`sck_outside_cs`）は、待機のレベル（CPOL）が変わる条件で1回まで、ほかはng。spi.slave: DUTの既定の端子のCH32RVSPISlave（CSは残りの線）を、プローブのspi-controllerが100 kHz、CSから最初のクロックまで20 µsで動かす。softspi.slaveはSoftSPIの4本のSoftSPISlave（同じAPI、1ビット1回の割り込み。SCKとCSは別のビット番号の線、同じなら飛ばす）を50 kHzで、同じ問い。4モードと2つのビット順で8バイトを用意して8クロック、モード0で8バイトの用意に10バイト（11バイト目からfillのバイト、`length` 10、残すのは8）、何も用意しないCS（fillのバイトだけ、`missed()`が1）。バスはWireSkeinの`spi`（MOSIはプローブが送ったもの、MISOはDUTが用意したものとfill）、DUTの結果とプローブが読んだものはここで比べる。spi.peripheral / softspi.peripheral: バスそのものをperipheralにする（`SPI_HAS_PERIPHERAL_MODE`。`beginTransaction(SPISettings(..., SPI_PERIPHERAL))`、`transfer()`はクロックを待つ）。CSはプローブが出し、DUTは見ない（端子は入力）。DUTが`transfer()`で待っている所へ、4モードと2つのビット順で4バイト（SPIは100 kHz、SoftSPIは50 kHz）。バスはWireSkeinの`spi`、DUTが読んだものとプローブが読んだものはここで比べる | CSの自動、DMA、2 MHzより速いクロック、バスを共有する別の機器、スレーブの上限のクロック、バイトの間を詰めたperipheralモード |
| usb-media / usb.audio-capture、usb.audio-playback、usb.video-bulk | usb と同じ配線。UAC1ヘッドセット（32 kHz 16 bit モノラル）とUVCカメラ（80x60 YUY2、バルク）。OSのドライバを外し、アイソクロナス転送はlibusbをctypesで直接使う（usb_iso.py）。マイクの連番が途切れないこと、スピーカーに送った100 msが数も和も一致すること、カメラのフレームが1バイト残らず期待どおりであること | OSのサウンド／ビデオ機器としての動作、UAC2、アイソクロナスのビデオ、長時間の同期 |
| usb-ss-iso / test_usb_ss_iso_in、test_usb_ss_iso_out | usb と同じ配線で、DUT の USBSS が PC と SuperSpeed でつながっていること（6-2。SS でなければ飛ばす）。ベンダーのインターフェースの代替設定ごとに、1024 バイト・125 µs ごとの等時の端点と、bMaxBurst / Mult が 1/0（1 burst の基準）・1/1・1/2・15/2（48 パケット、IN と OUT を別の設定）の companion。libusb（usb_iso.py）の 1 パケットが 1 間隔。IN は各間隔が全パケット（順番・間隔の番号・最後のバイト）か空か一部かを数え、OUT は同じ形のデータを送って DUT が数える。一部の間隔が 1 % 以下、全部そろった間隔が 90 % 以上で合格（Mult を出せなければ全間隔が一部になる）。15/2 の設定をホストが帯域で断ったら飛ばす | ハブ越し、長時間、等時の同期・適応、ストリーム |
| usb-peer-firmware / usb.peer-dfu-download、usb.peer-dfu-refuse、usb.peer-uf2-drive、usb.peer-uf2-refuse、usb.peer-raw-drive | usb-peer と同じ配線（相手は Pico、ESP32-S3 / P4、CH32RV のホスト）。ピアがイメージを 16 進の行（fwclr、fwput。fwclr がピアの配列の大きさを答え、入らない像は飛ばす）で受け、DFU（dfudn、dfuall）かドライブ（uf2w、rawall、mscej）で DUT に渡す。DUT 自身の .bin の世代の印（CH32GEN:）を変えて、DFU、UF2 のドライブ（最後のブロックから）、.bin のドライブ（順に書いて取り出し。Pico と CH32RV のピア: EspUsbHost に取り出しが無い）で送り返し、再起動後の印が送ったものであること。イメージでないブロック（errWRITE）、途中の ABORT、ほかの family の UF2 は断られ、印が変わらないこと。BOOT_HOLD 付きでビルドする（build_opt.h）。staging に入らない品番（V203 の ANY など）は飛ばす | dfu-util・ファイルマネージャでの操作、Windows、コピー中の電源断 |

現在の配線ではLink＋V103、LinkE＋V307、ESP32＋V003、追加のLinkE＋V003を対象にできます。追加のLinkE＋V003にUART配線は宣言していません。GPIOはESP32側のV003に設定したpadだけです。P4フル結線、USB対向機、未接続系列は配線・契約の確定後に追加します。

usbのホスト役は当面このPCです（`test_usb_contract.py` の `PcUsbHost`。キーボードの入力はHIDのカーネルドライバを外してから送らせるので、PCのデスクトップには届きません）。DUTは SWD で書き込み、DMSEQで指示します。ホスト役を別のマイコン（Pico）に替えるときは `PcUsbHost` だけを置き換えます。DUTはVBUSを使わず `selfPowered` で動かします。

UIAPduino V003は `upload_port = "ch32rv-hid://<soft_usb_topology>"` を設定します。runtime／UART／GPIOでも毎回SWIO＋NRSTからbootloaderへ移行し、HIDで書き込み、ESP32上のDMSEQとfixtureで観測します。RP2350は `upload_port = "oep://<usb_serial>/<port>"` を設定します（`<port>` は probe に保存したピンのラベル `S.swdio` などの `S`）。console用のserial pathとupload先をそれぞれ指定します。

## 実行とartifact

`run_hardware.py` は設定・環境の準備を行い、compile/upload/monitorとDUT lifecycleは固定したpytest pluginへ渡します。Pythonテストはsketchと別に保管し、実行時にout内へ組み立てるため、テスト収集だけでuploadは始まりません。Arduinoのuser/data/downloads、platform.local.txt、sketch、生成物、ログはout内に作り、作業ツリーのコアを使用します。通常のsketchbookや `.arduino15` は変更しません。

out内のArduino CLI wrapperはupload時のdiscovery timeoutを10秒にします。HID／OEPのcustom discoveryをCLI既定の1秒で打ち切らないためです。HID profileには `protocol: ch32rv-hid` も指定します。

実機時は共通ロックを全期間保持し、stable serial pathとUSB topology／VID／PID／serialを照合します。Link側はDUT UID、現在のOEP target.infoがUIDを返さない経路ではchip IDとSKUを照合します。OEPのchip IDは系列・構成の識別で、DUT個体のUID保証とは区別します。

Flashの退避は行わず、指定したテストsketchを書き込んで使用します。接続先情報、コアcommit/dirty、tool版、build ID、コマンド、pytestログ、DUTログ、JUnit、`result.json` を保存します。caseごとの失敗を記録して後続targetも処理します。

テスト後はテストsketchがDUTに残ります。自動復元は行いません。V003のsystem bootloader領域は操作しません。PPPSのOFF区間はfinallyでONへ戻し、GPIOはINPUTへ解放します。中断時はartifactと機材状態を確認してから再開してください。

固定Python依存は `pyproject.toml` の `hardware` extraと `uv.lock` で管理します。GPIO／UARTのplanは試験用session内で扱い、プローブfirmwareの更新や保存設定の変更は行いません。

### 排他

- 試験は同時に走らせません。pytest は 1 回の実行の中で試験を順に回し、試験の実行を同時に 2 つ起こすこともしません。この前提なので、試験の側でロックは取りません（`bench.lock` も使いません）。
- 機器の排他は pytest の機器ロック（`--device-lock required`）に任せます。WCH-Link / LinkE は Link の UART の tty、OEP のプローブ（`oep://` の DUT を含む）はプローブ自身の lease で排他します。ベンチの `bench.lock` は OEP の道具だけが使うもので、このリポジトリの道具も手作業も取りません。uhubctl にロックの決まりはありません。
- OEP のプローブはロックを 1 つ持ち、ロックを持つセッションだけが状態を変えられます（oep-spec core §6.1）。ch32rv はプローブごとのブローカーがこのセッションを持ち、ほかの ch32rv の処理はそれを共有します。手作業で ch32rv を叩くときは、試験の実行と重ねないことを運用で守ります。
- lease（ch32rv プラグインのセッションは 60 秒）より長く黙る試験は、セッションが切れます。長い待ちのある試験は keepalive の要る作りにします。

### プローブ版と責務

`result.json` の `host` は実際のArduino CLI・ch32rv（DB/stub digestを含む）・Python・plugin/client版とuv.lockのhashを記録します。`probes.<target>` と `preflight/<target>/probe.json` はプローブの申告版・model・個体情報を記録します。WCHはraw／正規化版／WCH表記とknown_badを、OEPはfirmware文字列をそのまま保存し、protocol revision、interface revisionとdescribe宣言も保存します。ホストclientの版とプローブfirmwareの版は別項目です。取得失敗・版なしは `metadata_status` とエラー／nullで明示し、推測で埋めません。compile-onlyではプローブ情報を取得しません。

このコアではプローブfirmwareの数値的な最低版を一律に指定しません。ケースの実行に必要な機能・interface revisionの互換性はch32rv／OEP client／pluginの判定と実行結果に従います。版やcapabilityの記録だけで互換性をPASSにしません。プローブ側の既知不良・対応条件・更新imageの検証はプローブ提供側、host依存の固定はこのsuiteのlockで管理します。新しい最低版が必要な不具合を見つけたときは、根拠となる契約と修正を所有リポジトリへ渡します。

## カバレッジ

どこまで実機で確かめたかは、`tests/coverage.toml` の項目と、`run_hardware.py` の実行結果（`--out` のディレクトリ）を
`tests/coverage.py` で突き合わせて見ます。

```
uv run --no-project python tests/coverage.py <--out のディレクトリ> ... [--markdown coverage.md]
```

- 項目には範囲が 2 つあります。`scope = "series"` は**系列ごとに 1 回は**通したいもの（系列ごとにドライバ、端子、
  クロックが違う: runtime、uart、gpio、flash、synckit、logic、spi、wire、route、pwm、adc、USB デバイス、USB ホスト、
  Ethernet、PD、SDIO、CAN）。`scope = "once"` は**全体で 1 回**通せばよいもの（ドライバの上の共通のコード、相手との
  手順: SoftSPI、SoftWire、SPI の SD、USB のクラスとデバイス側のピア試験、HID、電源）。I2S・ARGB・RS485・QSPI・CAN は
  予定の項目で、ドライバはまだありません（ラベルを付けると失敗として出ます）。
- `test` は 1 つか、そのリストです（どれが通っても数える）。USB デバイスは、相手が PC の `usb` と、ESP32-S3 の
  `usb-peer` のどちらでも数えます。1 つの項目は 1 つの問いです。case 全体を指す項目はどの関数が通っても ✓ になるので、
  問いを複数持つ case（uart、spi、wire、system、selfsys など）は、関数ごとに項目を分けます。
- `kind = "self"` の項目（selfcheck、selfbus、selfsys: DUT が自分の端子やレジスタを読み返す）は、外から観測した結果の
  代わりにならないので、別の表に出します。
- `test` の無い項目は、コアにあって実機の試験がまだ無い機能です。表には「no test」と出て、分母に入ります。
- `run_hardware.py`（harness/place.py）も同じ項目を読んで、その系列で流せる case を決めます。
- その周辺機能の無い系列は n/a、まだ走らせていない所は空欄です。最後の行が「最新の実行で通った系列の数 / 対象の系列の数」です。
- 結果は試験の関数ごとに junit から取ります（`<case>::<関数>[<引数>]`）。ラベルが足りずにスキップしたものは skip で、
  通ったことにはなりません。チップにその機能が無いと DUT が答えてスキップした試験は、理由を `n/a:` で始め、n/a として
  分母から外します。build や書き込みで pytest の前に落ちた case は、その case の全部の項目の ✗ に数えます。
- 周辺機能の有無は、variant のクロックの許可の名前（`needs` / `any_of`）で見ます。名前で拾えない系列は `also`、
  名前はあっても項目の試すものが無い系列（V103 の旧い USB に USB ホストは無い、など）は `not_on` に書きます。
  表は系列ごとです。系列の中で型番によって周辺が無いもの（variant の `CH32RV_<instance>_ABSENT`。CH32M030G8R7 の SPI など）は、
  `run_hardware.py` がその型番で流さないだけで、表の上では系列の欄に数えます。
- 系列の欄は、その項目を流した一番新しい実行の結果です（同じ実行の中では ✓ が ✗ より、✗ が skip より上）。それより
  前の実行によりよい結果があれば「✗ (✓ before)」のように後ろに書くので、前は通っていたものが落ちたのが分かります。
  実行の新しさは result.json の時刻です。
- 項目を足すときは `coverage.toml` に書きます。`tests/unit/test_coverage_report.py` が、どの項目も `run_hardware.py`
  の case を指していることを確かめます。

## 配置

| 場所 | 内容 |
|---|---|
| unit/ | 設定、identity、電源対象、収集範囲、保守ツールのボード不要検査 |
| platform/ | 固定sourceの整合検査。toolchainを使うplatform契約の配置先 |
| single/runtime/ | DUT runtimeとconsoleの契約 |
| instrumented/uart/、gpio/、power/、usb/ | 独立UART、GPIO刺激・観測、USB電源復帰、USBデバイス |
| sketch_support/core_contracts/、uart/、usb_contracts/、usb_classes/、usb_media/ | 実行時にstageするsketch |
| fixtures/、harness/ | 設定雛形と最小限の設定・identity照合 |
| loopback/、peer/、manual/、diagnostics/ | 今後の契約の配置先 |

full compile matrix、startup等価性、size baseline、未配線周辺機能は、このsuiteの成功で検証済みとは扱いません。
