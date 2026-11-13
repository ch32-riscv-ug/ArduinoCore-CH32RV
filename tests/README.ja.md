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

試験の相手は、アドレス 1 つで名指します。ch32rv に渡すのと同じもので、`tests/.env` の `CH32RV_PROBE` に書きます（[.env.example](.env.example)）。OEP の口なら `oep://<unit>/<口>`、WCH-Link なら `wchlink://<serial>` です。設備のファイル（bench.toml）はありません。ほかの事実は、本当にある場所から読みます（[harness/place.py](harness/place.py)）。

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
  --probe oep://30eda0e31108/x035 --probe wchlink://FB868F068059 --case gpio --out work/run1
```

`--probe` と `--out` は環境変数より優先します。`--out` を省略すると `OEP_HW_RESULTS` 内に実行ごとの新しいディレクトリを作ります。書込みには常に `--execute` が必要です。機器の排他は pytest の機器ロック（`--device-lock required`）が受け持ち、ベンチの共通ロックは取りません。`--case` で、その相手で流せない case を求めると、理由つきの SKIP になります。

### 記録（プローブのメモ欄）

個体ごとに違い、機器から読めない事実は、プローブのメモ欄（oep-spec oep-if-memo §4）に、口ごとに置きます。プローブについて行くので、プローブを別のホストにつないでも、そのまま試験できます。プローブは中身を読まないので、守らせる必要のある値は置きません。

| 名前 | 持ち主 | 中身 |
|---|---|---|
| `oep.probe.memo/port/<口>` | OEP（§4.3） | `target`（`<系統>-<UID>`、照合に使う）、`part`（チップ ID で決まらないときの型番）、`note` |
| `io.github.ch32-riscv-ug.arduino/port/<口>` | このコア | `board`（`<作り手>:<型>`）、`supply`（`{from: probe \| usb \| other}`）、`usb`（`{to: host}`、`{to: esp32-s3 \| esp32-p4, serial と mac の一方または両方}`、`{to: rp2040, port}`）、`parts`（`{sd: {bus, <信号>: <端子>}, eth: {to: lan}}`）、`pd`（`{max_mv, vbus_pin, vbus_ratio}`）、`defects`（`[{id, lose: [機能], why}]`）、`note`、`updated` |

名前、形、メンバーごとの決まり、読み方と書き方は [RECORDS.ja.md](RECORDS.ja.md) にあります（検査の実装は [harness/records.py](harness/records.py)）。書くときは `tests/bench_record.py set`（同じセッションの中で読んで書き、渡した項目だけを変え、ほかの道具の項目は残す）、見るときは `show` を使います。正しい中身はホストの側にも残してください。WCH-Link は記録を持てないので、記録の要る試験（sd、eth、usb の相手など）は流れません。ただし WCH-Link には、tests/.env の `CH32RV_PROBE_RECORD_<serial>`（`wchlink://<serial>` と同じ serial）で同じ形の記録を渡せます（[RECORDS.ja.md](RECORDS.ja.md) §5）。プローブが運べない console（WCH-Link(CH549) の SDI など）は、`ch32rv capabilities` の答えで飛ばします。チップ ID で型番が決まらない Link の相手には、`CH32RV_CHIP` を書きます。

```sh
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035s3 \
  --target auto --board weact:CH32X035-CoreBoard-V1.0 --supply usb --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c
```

| case / contract | 必要設備と確認するもの | 確認しないもの |
|---|---|---|
| runtime / runtime.init、runtime.time-progress、runtime.print-heap、console.round-trip | DUTとDMSEQ対応プローブ。毎回のbuild ID＋challenge応答、data/bss/constructor、Print/String/malloc、時間APIの粗い進行 | OOM・断片化、時間精度、全clock条件・wrap境界、全console方式 |
| uart / uart.external-round-trip、uart.output-only、uart.formats、uart.receive-errors | uart.output-only: uart.formatsと同じ条件を、プローブのUARTを付けずにDUTのTXだけ取り込んでデコードする（切り分けの基準）。標準UARTに接続した独立UART。115200 8N1、64 byte binaryの往復とstdio。uart.formats（OEPの口、ラベル`<口>.rx@<端子>`と`<口>.tx@<端子>`）: 9600・115200・1 Mbaudの8N1と、115200の8E1・8O1・8N2・7E1・7O1を、2本の線の1本の取り込み（5 MS/s）で、条件ごとのWireSkeinの区間にする。DUTが送る16バイトを`uart`でデコード（中身、形式、F_CPU÷BRRのボーレート3 %以内、フレーミングとパリティの誤りなし）、プローブのUARTが送る16バイトも同じくデコードし、DUTが受けた数とFNV-1aを比べる。条件の始めと終わりは両方の線がidle、送っていない側の線は動かない。uart.receive-errors: プローブのUARTの形式をずらして誤りを起こし（8E1にした DUT へ 8O1、8N1 の DUT へ偶数パリティの 8E1 と 0x00、読まずに 100 バイト、割り込みを止めたまま 16 バイト）、`Serial.onReceiveError()` が報告した種類（パリティ・フレーム・ブレーク・バッファ満杯・オーバーラン、誤りのない条件では何も）と DUT が残したバイト数を比べる。送った波形はプローブの形式で WireSkein がデコードする | ほかのinstance/route、5・6ビットと1.5ストップビット、3 Mbaudの端、ノイズ（NE）の再現 |
| gpio / gpio.external-levels | プローブのラベル`<口>@<端子>`（と、部品のつながらない役割の線）の全部。出力の外部観測と外部入力刺激、終了時INPUT。ラベルが無ければスキップ | 未配線pad、全package、analog/PWM/interrupt |
| signals / signals.pwm、signals.servo、signals.tone、signals.interrupt、signals.timer-input、signals.capture-after-pwm | プローブのラベル`<口>@<端子>`の線から1本（PWMとタイマーの入力は、DUTがvariantの表でタイマーの端子だと答えた線）と、隣の線2本。試験ごとに1本で続けて取り込み（`instrumented/ws_capture.py`のstream）、条件ごとにWireSkeinの区間`## <条件>`を開いて命令を送り、その区間に線がなる状態を`becomes`で書く（前の状態から1度だけ変わり、変化のあとはどの周期も範囲内）。外側の区間は`only_moving`で、隣の線が動かないこと。区間はプローブの時計で1本の取り込みから切り出され、どの条件で外れたかは1つの記録で分かる。判定はWireSkein（pytest-embedded-wireskein）: analogWriteの64・128・192で1 kHz、同じ値の2回目、255と0でHighとLow、128の2 kHzと500 Hz、止めた後のLow。Servoの544〜2400 µsと2400の2回目で50 Hz・その幅（30 µs）、detach後のLow。toneの440 Hz・2 kHz・2 kHzの2回目で50 %、noTone後のLow、1 kHz・30 msで30周期の後にLow。どの波形の条件も、変化のあとに20周期以上と、命令から50 ms以内の変化を求める。周波数は2 %。切り替えで切れてよい周期の数はコアのきまり: デューティとServoの幅は0、PWMの周波数は2、toneは1、出し始めと止めるとき（analogWriteの0と255を含む）は1。割り込み（RISING・FALLING・CHANGEの数）とCH32RVPulseCapture（プローブのPWMの周期とHighの時間）はDUTの答えを比べる。analogWriteを止めたあと（0、pinMode、digitalWrite）に同じ端子でCH32RVPulseCaptureが始められること（動いている間は断られること）も確かめる。試験ごとにDUTを再起動する | 2本以上の同時の出力、高い周波数、Servoの複数、入力に戻した後の線（プルが無いと浮くので見分けられない） |
| system / system.reset-reasons、system.watchdog、system.sleep | system.reset-reasons: `CH32RV.restart()` の後の`resetReason()`がSOFTWARE、プローブがNRSTを引いた後がEXTERNAL。system.watchdog と system.sleep: ラベル`<口>@<端子>`の線を2本（1本目は計時の線で、DUTが手放すとプローブの入力プルダウンでLow、2本目はプローブが動かす起こす線）、1本の取り込み（1 MS/s）に条件ごとのWireSkeinの区間。計時の線は計る間だけHigh: ウォッチドッグの最後の餌からリセットまで、眠っている間。そのHighの長さはWireSkeinの分析（states）から読み、ここで条件の範囲と比べて記録に書き戻す（`ws_run.result`）。ウォッチドッグ: `wdtEnable(250 / 1000)`を50 msごとに600 ms餌をやってリセットされないこと、そのあと餌を止めてHighの長さがタイムアウト（X033 / X035はHSI÷1024で -7 %〜+3 %、ほかはLSIで -60 %〜+70 %）、次の起動がWATCHDOG。スリープ: タイマー200 msの`lightSleep()`（OK、TIMER、`millis()`の進みがHighの長さの20 %以内、Highが0.1〜0.3 s）、起こす線をLowにして起きる`lightSleep()`（PIN）、タイマー200 msの`deepSleep()`（次の起動のwakeupCause()がTIMER） | POWERONとWWDGの要因、スタンバイの系列（V20x / V30x）のdeepSleep、ウォッチドッグを付けたままの眠り、4本の端子の起こし |
| console-rtt / console-dmdata / console-sdi | デバッグのコンソールをスケッチのただ1つのコンソールにする（`sketch_support/console`、ビルドのflag `CONSOLE_RTT` / `CONSOLE_DMDATA` / `CONSOLE_SDI`、モニターの`source`はケースのport_config）。RTTとDMDATAは双方向: nonceにビルドIDで答える、ランダムな文字列20行のecho（80文字まで）、番号とハッシュ付きの200行がそろって順に届く（RTTは`availableForWrite()`を見て待つ。`write()`は入らない分を捨てる仕様）。SDIは送るだけ: 100 msごとの行がビルドIDと番号を持ち、21行で番号が飛ばない。WCH-Link以外ではスキップ | SDIはV307のLinkEで届かない（DMDATAは同じ番号で通る、[ch32rv]に調査を依頼、2026-11-05）、RTTのバッファの大きさの変更、ホストがいないときの挙動 |
| analog / analog.dac | 配線なし。variantがDACの出力と名指す端子（`CH32RV_DAC1_PIN` / `CH32RV_DAC2_PIN`）ごとに、`analogWrite()`を既定の8ビットと`analogWriteResolution(12)`で満量程度の0・1/4・1/2・3/4・全部に書き、同じ端子を`analogRead()`で8回読む: 平均が書いた値からADCの満量程度の4 %以内、8回の幅が1 %以内。DACの無い部品はスキップ | 外部の電圧計での絶対値、負荷をつないだとき、DACの更新の速さ |
| hid / upload.hid-runtime | UIAPduino V003、ソフトUSB、OEP SWIO＋NRST。HID権限、Arduino upload recipe、独立DMSEQ上の新build IDとruntime | Arduino USB stack、USB PD、bootloader領域の書換え |
| power / usb.port-cycle | 指定PPPSハブ。対象だけのUSB消失、再列挙、個体照合、DUT再attach | VDDが0 Vまで落ちること、DUTのcold boot証明 |
| usb / usb.enumerate、usb.cdc-round-trip、usb.hid-composite、usb.hid-keyboard、usb.midi-round-trip、usb.net-frames、usb.reconnect、usb.isp-entry-dfu-detach、usb.isp-entry-touch | DUT自身のUSB端子（`dut_usb_topology`）をホスト役につなぐ。CH32RVUsbDevice の複合デバイス（CDC、キーボード＋ベンダーHID、MIDI、既定のDFUランタイム）の列挙とこのbuildの照合、CDC 64 byte binary、HIDのレポートID・ベンダー往復・キー入力、MIDI往復、NCMのMACどおりのインターフェースと双方向のフレーム（root不要）、`end()`→`begin()` での離脱と再列挙、DFU_DETACH と1200bpsタッチでのISP（1a86:55e0）移行と自動復帰 | 大量転送の速度、サスペンド・レジューム、VBUSに依存する動作、ISPでの書き込み、X035以外の系列 |
| usb-classes / usb.webusb-descriptors、usb.ms-os-20、usb.vendor-round-trip、usb.vendor-request、usb.msc-block、usb.msc-file、usb.ccid-apdu | usb と同じ配線。X035のエンドポイント数でusbと同居できないクラス（ベンダーのバルク、MSC、CCID）の複合デバイス。BOSのWebUSB・MS OS 2.0（WinUSBの互換IDとGUID）とWebUSBのURL、ベンダーのバルク往復とベンダーリクエスト、MSCのINQUIRY・容量・ブロックの書き込みと読み戻し、ホストが置いたFAT上のファイルをスケッチが読めること、CCIDの電源投入（ATR）とAPDU1往復 | Windows／ChromeでのWinUSB・WebUSBの実動作、PC/SC経由のCCID、MSCの速度、OSのFATドライバでのマウント |
| eth / eth.dhcp、eth.icmp、eth.udp-echo、eth.tcp-echo、eth.tcp-both-ways、eth.http-get、eth.https-verified、eth.https-wrong-ca | ETH のある系列（H417 で実機確認。V407 / V467、V307、V317、V208 / V203RB はビルドまで）の RJ45 をこのPCと同じLAN（DHCPあり）へ。DHCP、ping、UDP／TCPの往復の中身、両方向4 MiBの中身と量、HTTPのGETの本文のCRC、HTTPS（試験ごとにopensslで作るCAでECDSA／RSAのサーバーを検証、setInsecure、別のCAのサーバーは断られること）、試験中にPHYのリセットがないこと。速さはjunitのpropertyに残す。TLSの無い系列（V208／V203RB）はHTTPSの段を飛ばし、ヒープが足りない（-0x7F00、V307／V317）ときは記録して飛ばす | 10Mのリンク、リンクの抜き差し、長時間の連続、IPv6、H417以外の実機 |
| selfcheck / selfcheck.clocks、serial_transmit、serial_receive、spi、gpio、pwm、vref | 配線なし（LinkEのDUTでも流せる）。ビルドが前提とするバスのクロック（`CH32RV_PCLK2_HZ`）がRCC_CFGR0のPPRE2と一致。HardwareSerialの全部で、BRRが自分のバス（USART1はPCLK2、ほかはPCLK1）のクロック÷速度で、64バイトを送る時間が115200と1000000で1バイト10ビットの4 %以内。受信は、RXの端子をGPIOの出力にしてスケッチが9600で16バイト（先に捨てる0xFF）を打ち、全部が順に誤りなく届く。既定のSPIのBRと500 kHzでの時間。gpioは、その石の全部の端子から、device-dataの端子表のデバッグ・NRST・BOOT・水晶・USB（D+/D-、CC）を除き、プルアップ1・プルダウン0を読んでから（変わらない端子は外から押さえられているので駆動しない）、出力の読み返しと、出力にした端子で起こすRISING 5・FALLING 5・CHANGE 10回の割り込み。PWMは、タイマーのチャネルのある端子（除く端子・押さえられた端子・DACの端子を除く）で、`analogWrite()`の64と192を1 kHzと10 kHzで出し、同じ端子を50 ms読んで、周期の数が2 %以内、1の割合がdutyの4 %以内。`getVddMillivolts()`が2.7〜5.5 V。記録のdefectsが`gpio-drive`を失う基板（M030のモーター基板）では、端子を駆動する確認を飛ばす | どの端子に出るか、レベル、相手との実際の通信（配線のある試験で、系列・周辺ごとに代表1つ） |
| selfbus / selfbus.spi_out、spi_in、uart_formats、uart_errors、uart_transmit、i2c_registers、i2c_nack | 配線なし（selfcheckの続き。V003の16 KBに収まらないので別のスケッチ）。既定のSPIで0xA5と0x3Cを4つのモード・MSB／LSBで一番遅いクロックで送り、スケッチがSCKとMOSIを自分の端子の入力レジスタで読み続ける: SCKの待機レベルがCPOL、縁が16回、モードの読み取り側の縁で組み立てたMOSIのバイトが送ったバイト。MISOをGPIOの出力にして0／1を出し、受信が0x00／0xFF。除く端子・押さえられた端子があれば飛ばす。UARTは、RXの端子が空いている全部のポートで、8N1・8E1・8O1・7E1・8N2の8フレームが誤りなく届くこと、パリティ反転（PE）・ストップビット0（FE）・全部0（ブレーク）で捨てられて記録されること、割り込みを止めた3フレームでORE、読まずにバッファより多くでFULL。送信は各形式で1フレームのビット数どおりの時間（4 %以内）。I2Cは、`setClock()`の100 kHz／400 kHzの後でFREQがPCLK1のMHz（上限を超える系列は上限）、CKCFGRが標準・高速の式どおり。相手のいない送信が200 ms以内にエラーで返る（両方の線がプルアップされていれば2）。`gpio-drive`を失う基板（M030）は飛ばす | 実際の相手とのSPI・UART・I2C、SCKの最高速、DMA |
| selfsys / selfsys.identity、temperature、restart、watchdog、tone、servo、adc_pads、rtc、light_sleep | 配線なし。`getUniqueId()`がch32rvの読んだUIDと一致、温度センサーが0〜70 ℃、`restart()`の後の理由がsoftware、ウォッチドッグ（500 ms）で0.15〜1.5秒後にリセットされ理由がwatchdog。空いた端子で`tone(1000)`が100 msに100周期、Servoの1000／1500／2000 µsを`pulseIn()`で3 %以内。アナログの全部の端子を出力の1／0にして、直前に別のチャネルを変換してから読み、1で80 %以上（H41xのVIO18の端子は30 %）、0で10 %以下（記録のdefectsが失うチャネルは判定しない）。RTCの系列で`gettimeofday()`が3秒で10 %以内。`lightSleep()`がタイマーの200 msで戻る（最後に流す。戻らないとコンソールも戻らない）。deepSleepはsystemの試験に任せる | 実際の電圧での精度、消費電流、ピンでの復帰 |
| logic / logic.continuous、logic.burst | DUTだけ（配線なし）。スケッチが取り込み用とは別のタイマーのPWMを自分のピンに出し（DACの端子は除く。設定の`logic_pin`で指定もできる）、そのポートをCH32RVLogicCaptureで取り込む。1 MS/sと10 kHz（RAMの小さい系列は250 kS/sと2.5 kHz）で周期がちょうど100サンプル、取りこぼしなし、captureBurstの周期がそろうこと | 外部の信号、16本すべての入力、24本のポートの上位8本、USBで流しながら |
| pd / pd.attach、pd.caps、pd.fixed、pd.pps、pd.restart | DUTのUSB-CにUSB PDの充電器。5 Vの契約が5秒以内にできること、profile 0が固定5 V、設定の`pd_max_mv`（既定5000）までの固定の電圧と5 Vへの戻り、どこにもない電圧が断られて契約がそのままなこと、PPS（あれば）の下限と中間、中間を（`maintain()`を呼ばず）system serviceだけで12秒保つこと、契約中の`end()`／`begin()`のあとに契約が戻ること。`pd_vbus_pin`と`pd_vbus_ratio`（分圧の比）があればVBUSを測って10 %以内 | ソース（`USBPDSource`）、Hard Reset、PD 2.0だけの充電器、ケーブルの向きの両方、M030のUSBPD1 |
| synckit / synckit.queue、synckit.notify、synckit.ring、synckit.critical、taskkit.executor | DUTだけ（配線なし）。タイマーの更新割り込み10 kHzがCH32RVSyncKitのQueue・Notify・Ringへ入れ、64 bitの値（上下の半分が常に等しい）を進める間、loop()が3秒取り出して確かめる。断られた分以外に欠け・重複・順番の乱れがないこと、CriticalSectionで読んだ64 bitが割れないこと、その区間が50 µs未満。守らずに読んだ値の割れは対照として記録。taskkit.executor: 同じ10 kHzの割り込みがpostするWork、10 msのPeriodで起きるWork、50 msのDeadlineで起きるWorkを1 sの間loop()のrunOnce()で回す。postはまとまる（走る数はpostの数以下で1以上）、最後の走りはすべてのpostの後、Periodの走りと取りこぼしの和が100、Deadlineの仕事は1回で遅れ5 ms以内 | 複数のISR、優先度の違う割り込みの入れ子、H417の2コア、LoopOnlyの誤用の検出 |
| flash / flash.units、flash.kept、flash.time | DUTだけ（配線なし）。`ch32rv_flash.h` の5つの関数を末尾の作業用の領域で試し（消す、書く、消して書く、2単位の消去、境界にそろっていないデータからの書き込み）、プログラム自身・単位にそろわない番地・フラッシュの外・長さ0を断ること。乱数の値を保存してソフトのリセットのあとに`setup()`が読めること。1回の`rewrite_page()`の時間と、その間の`millis()`・`micros()`の進みを記録。1回の実行で末尾の消去が約16回 | 電源断、寿命、書き込みの経路で保存が残るか |
| wire / wire.output-only、wire.repeated-start、wire.slave | プローブのラベル`<口>@<端子>`の線で、DUTが答えたWireの既定のSDA / SCL（どちらもラベルがあること）。1本の取り込み（2 MS/s）に条件ごとのWireSkeinの区間を置き、`i2c`が判定する。区間の始めと終わりは両方の線がHigh。output-only: 相手なしで、プローブの端子の入力プルアップだけ。2つのアドレスに1バイト書き、線の上はS、ACKの無いアドレス、P、`endTransmission()`は2。repeated-start: プローブのi2c-target（0x42、内部プルアップ）を相手に、100 kHzで0x10を`endTransmission(false)`で書き、`requestFrom(addr, n, stop)`でn（1、2、3。読みの3つの経路）バイト読み、0x20を`endTransmission(true)`で書く。トランザクションごとの始まりと終わり（stop 0でS W Sr R Sr W P、stop 1でS W Sr R P S W P）、バイトとACK、バスの解放。DUTの戻り値（0、0）と読んだバイトは試験の側で比べる。fqbnでそのI2CをSoftware（`i2c1=software`）にすれば、メニューのSoftwareのWireを確かめる。wire.slave（wireのみ）: `Wire.begin(0x42)`をプローブのi2c-controllerが100 kHzで動かす。3バイトの書き込みはSTOPのあと`onReceive(3)`、`onRequest()`の4バイトの読み、1バイト書いてrepeated STARTで2バイト読むと`onReceive`が先で`onRequest`が後、用意より多く読むと残りは0xFF、40バイトの書き込みはバッファの32バイト（頭）、別の番地は応答なし。バスはWireSkeinの`i2c`、コールバックが見たものとプローブが読んだものはここで比べる（ベンチのX035はI2Cの使えない個体なので、使えるI2Cのある対象で流す） | 400 kHz、クロックの引き延ばし、アービトレーション、Wireのスレーブ |
| softwire / wire.output-only、wire.softwire | wireと同じ2つの試験を、Wireの端子でない`<口>@<端子>`の線2本のSoftWireで流す（ハードウェアのrouteでなくてよい） | wireと同じ |
| sd / sd.spi、sd.sdio | ベンチのSDカード（FATのボリュームのあるもの）を、SDライブラリ（ラベル`spisd_*`）とSD_MMC（ラベル`sdio_*`、`sdio_d1`〜`d3`があれば4本）で識別・マウントし、64 KBを書いて読み戻し、ルートを一覧して消す。書きと読みの時間を記録。ラベルが無い方はスキップ | カードの種類ごとの違い、抜き差し、満杯 |
| spi / spi.output-only、spi.modes、spi.slave、softspi / softspi.output-only、softspi.modes | spi.output-only: spi-targetを付けず、プローブはCS・SCK・MOSIを取り込むだけ（MISOは浮くので判定しない）。下と同じ条件・同じ判定で、CSがHighの間のSCKは、待機のレベル（CPOL）が変わるときの1回の移りだけ許す。不具合がDUTか相手かを切り分ける基準。spi.modes: プローブのラベル`<口>@<端子>`の線: ハードウェアのSPIはDUTが答えた既定のSCK / MISO / MOSI（どれもラベルがあること）と残りの線のCS、SoftSPIは4本。OEPのspi-targetにつなぐ。モード0〜3とMSBFIRST / LSBFIRST（プローブが対応すれば）を250 kHzで、モード0のMSBFIRSTを1 MHzで、`transfer()`の3バイトと`transfer16()`の1語を送る。4本を1本の取り込み（10 MS/s）にし、条件ごとのWireSkeinの区間で`spi`が判定する: 線から読んだモード、CSがLowの間に40クロック、SCKのグリッチなし、MOSIのバイトと語（MSBFIRSTで上位から、LSBFIRSTで下位から）、MISOの用意したバイト、区間の始めと終わりにCSがHigh。DUTが読んだ値はDUTの答えと比べる。CSがHighの間のSCKの動き（WireSkeinの注意`sck_outside_cs`）は、待機のレベル（CPOL）が変わる条件で1回まで、ほかはng。spi.slave（spiのみ）: DUTの既定の端子のCH32RVSPISlave（CSは残りの線）を、プローブのspi-controllerが100 kHz、CSから最初のクロックまで20 µsで動かす。4モードと2つのビット順で8バイトを用意して8クロック、モード0で8バイトの用意に10バイト（11バイト目からfillのバイト、`length` 10、残すのは8）、何も用意しないCS（fillのバイトだけ、`missed()`が1）。バスはWireSkeinの`spi`（MOSIはプローブが送ったもの、MISOはDUTが用意したものとfill）、DUTの結果とプローブが読んだものはここで比べる | CSの自動、DMA、2 MHzより速いクロック、バスを共有する別の機器、スレーブの上限のクロック |
| usb-media / usb.audio-capture、usb.audio-playback、usb.video-bulk | usb と同じ配線。UAC1ヘッドセット（32 kHz 16 bit モノラル）とUVCカメラ（80x60 YUY2、バルク）。OSのドライバを外し、アイソクロナス転送はlibusbをctypesで直接使う（usb_iso.py）。マイクの連番が途切れないこと、スピーカーに送った100 msが数も和も一致すること、カメラのフレームが1バイト残らず期待どおりであること | OSのサウンド／ビデオ機器としての動作、UAC2、アイソクロナスのビデオ、長時間の同期 |

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
  クロックが違う: runtime、uart、gpio、flash、synckit、logic、spi、wire、uart2、pwm、adc、USB デバイス、USB ホスト、
  Ethernet、PD、SDIO、CAN）。`scope = "once"` は**全体で 1 回**通せばよいもの（ドライバの上の共通のコード、相手との
  手順: SoftSPI、SoftWire、SPI の SD、USB のクラスとデバイス側のピア試験、HID、電源）。I2S・ARGB・RS485・QSPI・CAN は
  予定の項目で、ドライバはまだありません（ラベルを付けると失敗として出ます）。
- `test` は 1 つか、そのリストです（どれが通っても数える）。USB デバイスは、相手が PC の `usb` と、ESP32-S3 の
  `usb-peer` のどちらでも数えます。
- `test` の無い項目は、コアにあって実機の試験がまだ無い機能です（DAC、Wire のスレーブ、HSADC）。
  表には「no test」と出て、分母に入ります。
- `run_hardware.py`（harness/place.py）も同じ項目を読んで、その系列で流せる case を決めます。
- 系列の表は、系列ごとに一番よい結果（✓ が ✗ より、✗ が skip より上）を出します。その周辺機能の無い系列は n/a、
  まだ走らせていない所は空欄です。最後の行が「通った系列の数 / 対象の系列の数」です。
- 結果は試験の関数ごとに junit から取ります（`<case>::<関数>[<引数>]`）。ラベルが足りずにスキップしたものは skip で、
  通ったことにはなりません。
- 周辺機能の有無は、variant のクロックの許可の名前（`needs` / `any_of`）で見ます。名前で拾えない系列は `also`、
  名前はあっても項目の試すものが無い系列（V103 の旧い USB に USB ホストは無い、など）は `not_on` に書きます。
  表は系列ごとです。系列の中で型番によって周辺が無いもの（variant の `CH32RV_<instance>_ABSENT`。CH32M030G8R7 の SPI など）は、
  `run_hardware.py` がその型番で流さないだけで、表の上では系列の欄に数えます。
- 渡した実行の全部から一番よい結果を取るので、古い実行の ✗ は、あとで通るまで残ります。今の状態を見るときは、
  今のコミットの実行だけを渡します。
- 項目を足すときは `coverage.toml` に書きます。`tests/unit/test_coverage_report.py` が、どの項目も `run_hardware.py`
  の case を指していることを確かめます。

## 配置

| 場所 | 内容 |
|---|---|
| unit/ | 設定、identity、電源対象、収集範囲、保守ツールのボード不要検査 |
| platform/ | 固定sourceの整合検査。toolchainを使うplatform契約の配置先 |
| single/runtime/ | DUT runtimeとconsoleの契約 |
| instrumented/uart/、gpio/、power/、usb/ | 独立UART、GPIO刺激・観測、USB電源復帰、USBデバイス |
| sketch_support/core_contracts/、usb_contracts/、usb_classes/、usb_media/ | 実行時にstageするsketch |
| fixtures/、harness/ | 設定雛形と最小限の設定・identity照合 |
| loopback/、peer/、manual/、diagnostics/ | 今後の契約の配置先 |

full compile matrix、startup等価性、size baseline、未配線周辺機能は、このsuiteの成功で検証済みとは扱いません。
