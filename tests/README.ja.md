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
| `io.github.ch32-riscv-ug.arduino/port/<口>` | このコア | `board`（`<作り手>:<型>`）、`supply`（`{from: probe \| usb \| other}`）、`usb`（`{to: host}`、`{to: esp32-s3, serial, mac}`）、`parts`（`{sd: {bus, <信号>: <端子>}, eth: {to: lan}}`）、`pd`（`{max_mv, vbus_pin, vbus_ratio}`）、`defects`（`[{id, lose: [機能], why}]`）、`note`、`updated` |

形の決まりと検査は [harness/records.py](harness/records.py) にあります。書くときは `tests/bench_record.py set`（同じセッションの中で読んで書き、渡した項目だけを変え、ほかの道具の項目は残す）、見るときは `show` を使います。正しい中身はホストの側にも残してください。WCH-Link は記録を持てないので、記録の要る試験（sd、usb の相手など）は流れません。チップ ID で型番が決まらない Link の相手には、`CH32RV_CHIP` を書きます。

```sh
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035s3 \
  --target auto --board weact:CH32X035-CoreBoard-V1.0 --supply usb --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c
```

| case / contract | 必要設備と確認するもの | 確認しないもの |
|---|---|---|
| runtime / runtime.init、runtime.time-progress、runtime.print-heap、console.round-trip | DUTとDMSEQ対応プローブ。毎回のbuild ID＋challenge応答、data/bss/constructor、Print/String/malloc、時間APIの粗い進行 | OOM・断片化、時間精度、全clock条件・wrap境界、全console方式 |
| uart / uart.external-round-trip | 標準UARTに接続した独立UART。115200 8N1、64 byte binaryの往復 | 他instance/route/baud/format、overflow、波形精度 |
| gpio / gpio.external-levels | プローブのラベル`<口>@<端子>`（と、部品のつながらない役割の線）の全部。出力の外部観測と外部入力刺激、終了時INPUT。ラベルが無ければスキップ | 未配線pad、全package、analog/PWM/interrupt |
| hid / upload.hid-runtime | UIAPduino V003、ソフトUSB、OEP SWIO＋NRST。HID権限、Arduino upload recipe、独立DMSEQ上の新build IDとruntime | Arduino USB stack、USB PD、bootloader領域の書換え |
| power / usb.port-cycle | 指定PPPSハブ。対象だけのUSB消失、再列挙、個体照合、DUT再attach | VDDが0 Vまで落ちること、DUTのcold boot証明 |
| usb / usb.enumerate、usb.cdc-round-trip、usb.hid-composite、usb.hid-keyboard、usb.midi-round-trip、usb.net-frames、usb.reconnect、usb.isp-entry-dfu-detach、usb.isp-entry-touch | DUT自身のUSB端子（`dut_usb_topology`）をホスト役につなぐ。CH32RVUsbDevice の複合デバイス（CDC、キーボード＋ベンダーHID、MIDI、既定のDFUランタイム）の列挙とこのbuildの照合、CDC 64 byte binary、HIDのレポートID・ベンダー往復・キー入力、MIDI往復、NCMのMACどおりのインターフェースと双方向のフレーム（root不要）、`end()`→`begin()` での離脱と再列挙、DFU_DETACH と1200bpsタッチでのISP（1a86:55e0）移行と自動復帰 | 大量転送の速度、サスペンド・レジューム、VBUSに依存する動作、ISPでの書き込み、X035以外の系列 |
| usb-classes / usb.webusb-descriptors、usb.ms-os-20、usb.vendor-round-trip、usb.vendor-request、usb.msc-block、usb.msc-file、usb.ccid-apdu | usb と同じ配線。X035のエンドポイント数でusbと同居できないクラス（ベンダーのバルク、MSC、CCID）の複合デバイス。BOSのWebUSB・MS OS 2.0（WinUSBの互換IDとGUID）とWebUSBのURL、ベンダーのバルク往復とベンダーリクエスト、MSCのINQUIRY・容量・ブロックの書き込みと読み戻し、ホストが置いたFAT上のファイルをスケッチが読めること、CCIDの電源投入（ATR）とAPDU1往復 | Windows／ChromeでのWinUSB・WebUSBの実動作、PC/SC経由のCCID、MSCの速度、OSのFATドライバでのマウント |
| eth / eth.dhcp、eth.icmp、eth.udp-echo、eth.tcp-echo、eth.tcp-both-ways、eth.http-get、eth.https-verified、eth.https-wrong-ca | ETH のある系列（H417 で実機確認。V407 / V467、V307、V317、V208 / V203RB はビルドまで）の RJ45 をこのPCと同じLAN（DHCPあり）へ。DHCP、ping、UDP／TCPの往復の中身、両方向4 MiBの中身と量、HTTPのGETの本文のCRC、HTTPS（試験ごとにopensslで作るCAでECDSA／RSAのサーバーを検証、setInsecure、別のCAのサーバーは断られること）、試験中にPHYのリセットがないこと。速さはjunitのpropertyに残す。TLSの無い系列（V208／V203RB）はHTTPSの段を飛ばし、ヒープが足りない（-0x7F00、V307／V317）ときは記録して飛ばす | 10Mのリンク、リンクの抜き差し、長時間の連続、IPv6、H417以外の実機 |
| logic / logic.continuous、logic.burst | DUTだけ（配線なし）。スケッチが取り込み用とは別のタイマーのPWMを自分のピンに出し（DACの端子は除く。設定の`logic_pin`で指定もできる）、そのポートをCH32RVLogicCaptureで取り込む。1 MS/sと10 kHz（RAMの小さい系列は250 kS/sと2.5 kHz）で周期がちょうど100サンプル、取りこぼしなし、captureBurstの周期がそろうこと | 外部の信号、16本すべての入力、24本のポートの上位8本、USBで流しながら |
| pd / pd.attach、pd.caps、pd.fixed、pd.pps、pd.restart | DUTのUSB-CにUSB PDの充電器。5 Vの契約が5秒以内にできること、profile 0が固定5 V、設定の`pd_max_mv`（既定5000）までの固定の電圧と5 Vへの戻り、どこにもない電圧が断られて契約がそのままなこと、PPS（あれば）の下限と中間、中間を（`maintain()`を呼ばず）system serviceだけで12秒保つこと、契約中の`end()`／`begin()`のあとに契約が戻ること。`pd_vbus_pin`と`pd_vbus_ratio`（分圧の比）があればVBUSを測って10 %以内 | ソース（`USBPDSource`）、Hard Reset、PD 2.0だけの充電器、ケーブルの向きの両方、M030のUSBPD1 |
| synckit / synckit.queue、synckit.notify、synckit.ring、synckit.critical | DUTだけ（配線なし）。タイマーの更新割り込み10 kHzがCH32RVSyncKitのQueue・Notify・Ringへ入れ、64 bitの値（上下の半分が常に等しい）を進める間、loop()が3秒取り出して確かめる。断られた分以外に欠け・重複・順番の乱れがないこと、CriticalSectionで読んだ64 bitが割れないこと、その区間が50 µs未満。守らずに読んだ値の割れは対照として記録 | 複数のISR、優先度の違う割り込みの入れ子、H417の2コア、LoopOnlyの誤用の検出 |
| flash / flash.units、flash.kept、flash.time | DUTだけ（配線なし）。`ch32rv_flash.h` の5つの関数を末尾の作業用の領域で試し（消す、書く、消して書く、2単位の消去、境界にそろっていないデータからの書き込み）、プログラム自身・単位にそろわない番地・フラッシュの外・長さ0を断ること。乱数の値を保存してソフトのリセットのあとに`setup()`が読めること。1回の`rewrite_page()`の時間と、その間の`millis()`・`micros()`の進みを記録。1回の実行で末尾の消去が約16回 | 電源断、寿命、書き込みの経路で保存が残るか |
| wire / wire.repeated-start | OEPのi2c-targetとロジックの取り込みを、Wireの既定の端子（ラベル`i2c_sda` / `i2c_scl`。無ければスキップ）の2本に重ねる。100 kHzで0x10を`endTransmission(false)`で書き、`requestFrom(addr, n, stop)`でn（1、2、3。読みの3つの経路）バイト読み、0x20を`endTransmission(true)`で書く。読んだバイトと相手が受けた2つの書き込みのバイト（プローブがフレームを1つにつなげることがあるので、分かれ方は記録だけ）、線の上の条件がstop 0でS Sr Sr P、stop 1でS Sr P S Pであること。fqbnでそのI2CをSoftware（`i2c1=software`）にすれば、メニューのSoftwareのWireを確かめる | 400 kHz、相手の不在・クロックの引き延ばし・アービトレーション、Wireのスレーブ |
| softwire / wire.softwire | wireと同じ手順を、プローブのラベル`softwire_sda` / `softwire_scl`の線（ハードウェアのrouteでなくてよい）のSoftWireで流す。ラベルが無ければスキップ | wireと同じ |
| sd / sd.spi、sd.sdio | ベンチのSDカード（FATのボリュームのあるもの）を、SDライブラリ（ラベル`spisd_*`）とSD_MMC（ラベル`sdio_*`、`sdio_d1`〜`d3`があれば4本）で識別・マウントし、64 KBを書いて読み戻し、ルートを一覧して消す。書きと読みの時間を記録。ラベルが無い方はスキップ | カードの種類ごとの違い、抜き差し、満杯 |
| spi / spi.modes、softspi / softspi.modes | ハードウェアのSPIを既定の端子で（ラベル`spi_sck` / `spi_miso` / `spi_mosi` / `spi_cs`、既定の端子と同じであること）、またはラベル`softspi_*`の線のSoftSPIで、OEPのspi-targetにつなぐ。ラベルが無ければスキップ。モード0〜3とMSBFIRST / LSBFIRST（プローブが対応すれば）で、`transfer()`の3バイトと`transfer16()`の1語を送る。判定は線の上（プローブのロジックの取り込みでCS・SCK・MOSI）: CSがLowの間に40クロック、バイトと語（MSBFIRSTで上位から、LSBFIRSTで下位から）。読んだMISOのバイトと語。P4のspi-targetがときどき41ビット数えるときは、そのMISOのずれも含めて記録だけ | 速さ（SoftSPIは周波数を持たない）、CSの自動、DMA |
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
  クロックが違う: runtime、uart、gpio、flash、spi、wire、uart2、pwm、adc、USB デバイス、Ethernet、PD、SDIO、CAN）。
  `scope = "once"` は**全体で 1 回**通せばよいもの（ドライバの上の共通のコード、相手との手順: SoftSPI、SoftWire、
  SPI の SD、USB のピア試験、HID、電源）。
- 系列の表は、系列ごとに一番よい結果（✓ が ✗ より、✗ が skip より上）を出します。その周辺機能の無い系列は n/a、
  まだ走らせていない所は空欄です。最後の行が「通った系列の数 / 対象の系列の数」です。
- 結果は試験の関数ごとに junit から取ります（`<case>::<関数>[<引数>]`）。ラベルが足りずにスキップしたものは skip で、
  通ったことにはなりません。
- 周辺機能の有無は、variant のクロックの許可の名前（`needs` / `any_of`）で見ます。名前で拾えない系列は `also` に書きます。
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
