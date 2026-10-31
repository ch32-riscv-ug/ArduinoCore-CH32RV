# USB の使い方（デバイス・ホスト・PD）

このコアの USB は、次の3つのライブラリで構成します。デバイスもホストもこのコア自身のスタックで、TinyUSB は使いません（[ADR-0012](adr/0012-usb-stack.ja.md)）。

| ライブラリ | 役割 | スケッチから |
|---|---|---|
| [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/) | チップの USB をデバイスとして使う（CDC・HID・MIDI・ベンダー・MSC・CCID・ネットワーク・オーディオ・ビデオ・DFU ランタイム） | `#include <CH32RVUsbDevice.h>` |
| [CH32RVUsbPd](../libraries/CH32RVUsbPd/) | USB-C の PD 充電器に電圧を頼む（シンク）、PD で電圧を出す（ソース） | `#include <CH32RVUsbPd.h>` |
| [CH32RVUsbHost](../libraries/CH32RVUsbHost/) | チップの USB をホストとして使う（HID・CDC シリアル・MIDI・MSC・ベンダー・CCID） | `#include <CH32RVUsbHost.h>` |

ホストは [CH32RVUsbHost](../libraries/CH32RVUsbHost/)（`#include <CH32RVUsbHost.h>`）です（[ホスト](#ホストch32rvusbhost)）。
ESP32 の EspUsbDevice v3 と API をどう合わせたかは、[EspUsbDevice v3 との API の合わせ方](usb-espusbdevice-v3.ja.md) にあります。

## サンプルから始める

各サンプルフォルダに日本語と英語の README があり、必要な機器、配線、操作、期待するログ、設定値と制約を記載しています。
共通の配線とビルド方法を確認してから、個別の例へ進んでください。

| 目的 | 最初の例 | 全サンプルと接続手順 |
|---|---|---|
| ボードを PC の USB Device にする | [SerialUSBEcho](../libraries/CH32RVUsbDevice/examples/SerialUSBEcho/README.ja.md) | [Device 共通ガイドとサンプル一覧](../libraries/CH32RVUsbDevice/examples/README.ja.md) |
| ボードへ USB 周辺機器を接続する | [DeviceInfo](../libraries/CH32RVUsbHost/examples/DeviceInfo/README.ja.md) | [Host 共通ガイドとサンプル一覧](../libraries/CH32RVUsbHost/examples/README.ja.md) |
| PD 充電器/電源を扱う | [ListProfiles](../libraries/CH32RVUsbPd/examples/ListProfiles/README.ja.md) | [PD 共通ガイドとサンプル一覧](../libraries/CH32RVUsbPd/examples/README.ja.md) |

`Serial` はハードウェア UART、`SerialUSB` / Device の `UsbSerial` は USB CDC、Host の `CH32RVUsbHostCdcSerial` は
接続した USB シリアル機器への窓口です。ログを見る端子と、テストする USB データ端子を区別します。
Device では WeAct X035F8U6、Host では X035C8T6 を実機基準にしており、同じ X035 でも Host の有無が異なります。

## 対応状況

### コントローラとドライバの一覧（デバイス側、2026-10-21）

USB のコントローラを持つ系列と、それぞれの IP の種類、ドライバの状態です。
- 系列ごとのコントローラは、device-data の features.csv で確かめました。
- IP の種類は、EVT の構造体の配置を比べて分けました。
- 「実機」は、このコアのドライバでつないで確かめたものです。

| IP | 系列 | ドライバ | 状態 |
|---|---|---|---|
| USBFS（X035 型） | X035 / X033 | `CH32RVUsbDcdUsbfsX03x.cpp` | 実機（X035） |
| USBFS（USBOTG_FS 型） | H417 / H416 / H415 | `CH32RVUsbDcdUsbfsOtg.cpp` | 実機（H417） |
| 〃 | V203 / V208（PB6 / PB7）、V303 / V305 / V307 / V317、V205、L103 / M103、M030 | 〃 | ビルドのみ |
| USB（旧 IP） | V103 | `CH32RVUsbDcdUsbV103.cpp` | ビルドのみ |
| USBD（fsdev 型、CAN と共用） | V203 / V208 | `CH32RVUsbDcdUsbd.cpp`（`Usbd` を指定） | ビルドのみ |
| USBHS 第 1 世代 | V305 / V307 / V317 | `CH32RVUsbDcdUsbhs1.cpp`（`HighSpeed`、HSE が要る） | ビルドのみ |
| USBHS 第 2 世代 | H417 / H416 / H415、V205、X305 / X315、V407 / V467（2 基、2 基目は `HighSpeed2`） | `CH32RVUsbDcdUsbhs2.cpp` | ビルドのみ |
| USBSS | H417 / H416 | `CH32RVUsbDcdUsbss.cpp` | 実機（H417、5000M で列挙） |
| 〃 | X315（デバイスのみ） | 〃（`SuperSpeed`） | ビルドのみ |

`Auto` は系列の既定 controller を選びます。必ずしも最速の controller ではありません。
端子・クロック・他機能との兼用を確認して選択します。選択規則の実装は
[CH32RVUsbDevicePort.h](../libraries/CH32RVUsbDevice/src/CH32RVUsbDevicePort.h) にあります。

V003 / V002 / V004 / V005 / V006 / V007 / M007 には USB のコントローラがありません。

48 MHz の作り方は系列ごとに違います（`CH32RVUsbDcdUsbfsOtg.cpp` の冒頭）。
- **V20x / V30x:** システムクロックの PLL（96 MHz）を、USBPRE で 2 分周します。
- **L103 / M103:** 方式は V20x / V30x と同じです。CPU Clock が 48 / 72 / 96 MHz のときだけ作れます（既定の 8 MHz では `begin()` が失敗します。PLL の項目は実機で未確認）。
- **V205:** USBHS PLL を使います。HSE がなければ HSI を基準にします。
- **M030:** 固定倍率の PLL を立ち上げて使います。
- **H41x:** USBHS PLL を 10 分周します。

| 系列 | デバイス | PD | 確認 |
|---|---|---|---|
| CH32X035 | 動く（USBFS） | シンクとソース | X035（WeAct CH32X035F8U6 の板）で実機確認（PD ソースはビルドのみ） |
| CH32X033 | 動く（USBFS） | なし（CC の端子が無い） | X035 と同じダイ |
| CH32H417 / H416 / H415 | USBFS（PA11 / PA12）、USBHS、H417 / H416 は USBSS（SuperSpeed） | シンクとソース（PB3 / PB4） | FS は H417 で CDC の送受信を確認。SS は 5000M で列挙、CDC、EP0 の 512 バイトまでの応答、SS 経由の DFU を確認。USBHS と PD はビルドのみ |
| CH32L103 / M103、V205、X305 / X315 | 上のコントローラの表のとおり | シンクとソース | ビルドのみ。L103 / M103 は CPU Clock の 8 MHz ではデバイスの `begin()` が失敗する |
| CH32M030 | USBFS | シンクとソース（USBPD0、PA0 / PA1） | ビルドのみ |
| ほかの系列 | 上のコントローラの表のとおり | なし | ビルドのみ |

ディスクリプタとクラスは Full / High / SuperSpeed のどれでも組めるようにしてあります。

H417 / H416 は USBSS（`CH32RVUsbDcdUsbss.cpp`）で、SuperSpeed のホストにだけつながります。
- **USB 2.0 に切り替える経路はありません。** USB 2.0 の線（USBHS、PB8 / PB9）は SWCLK / SWDIO と兼用で、
  デバッグ機能は止められません（TinyUSB PR #3779 の実測）。WCH-Link をつないだまま USB 2.0 を使えないためです。
- USBHS は USBSS とは別のドライバです。実装・確認状況は上の一覧を参照してください。
- **USBFS（PA11 / PA12）は `CH32RVUsbDcdUsbfsOtg.cpp`。** V20x / V30x の USBFS と同じレジスタ配置。
  `config.controller = CH32RVUsbController::FullSpeed` で選びます。48 MHz は USBHS PLL（HSE から）÷10。
- **書き込みは DFU で行えます（プローブ不要）。** H41x はプログラムを RAM で動かすので、動いているスケッチが
  自分の flash を書き換えられます。DFU_DETACH で DFU モード（DFU のインターフェースだけ）として出直し、
  `dfu-util -d 1209:0001 -D <sketch>.bin` の DNLOAD を flash の先頭から書いて、最後にリセットします。
  FS と SS のどちらでも同じです。H417 の実機で、FS 経由の書き換えと SWD での照合を確かめました（2026-10-21）。
- **工場出荷のブートローダー（ROM の ISP）は、ソフトウェアからは入れません。**
  - 入る条件は、電源投入時に DP（PA12）が high で DM（PA11）が low のときです（評価ボードの説明書 §5）。
  - option byte の USER の bit6 / bit7（USBFSDLEN / USARTDLEN）が 0 だと、ISP は開きません。
  - BOOT_MODE を立ててリセットすると BOOT 域を通りますが、そこからはそのままスケッチが 0x08000000 から起動します
    （起動処理はこの経路でも動くようにしてあります）。
  - `enterBootloader()` は普通のリセットです。
- **USB 2.0 の線（USBHS）と SWD は同じピン（PB8 / PB9）です。**
  - 評価ボードでは、USB3.0-A の D+ / D− とデバッグ用ヘッダー（P3）に直結されています。
  - デバッグのピンを別のピンへ移す設定はありません。
  - AFIO_PCFR1 の SW_CFG = 100b で SDI を止められますが、止めても SWD が使えなくなるだけです。
  - USB3.0-A を PC に挿していると、ホストが D+ / D− を見て SWD を乱すため、プローブで書けません。そのため、
    書き込みは DFU で行い、プローブは USB を外した状態での復旧用と考えます（NRST を配線すれば
    `ch32rv --connect-under-reset` が使えます）。
- **USBSS の PLL は HSE を基準にします。** 25 MHz の水晶を前提にしており、HSE が立たなければ
  `begin()` が失敗します。
- **SuperSpeed の速さ（2026-10-21、CH32H417、Linux の xHCI に直結）:** 32 KB の `transfer()` を完了のたびに出し直すと、
  バルク IN 246 MB/s、OUT 233 MB/s で、データはすべて一致しました。バルクのエンドポイントは bMaxBurst 3
  （4 パケット）で記述します。0 だと IN 205 MB/s、OUT 138 MB/s でした。同じ測り方で、FS は IN 1.0 MB/s、
  OUT 0.7 MB/s です。Vendor クラスの `write()` は 1 バイトずつ FIFO に入れるので、ここまでは出ません。
  `writeDirect()`（16 KB のバッファ 2 つを完了の通知から交互に）で IN 194〜206 MB/s、`onRxData()` に
  32 KB の受信バッファ（当時の `setRxDataBuffer()`、今は `configure()`）を与えて OUT 52 MB/s でした（受け取った側の `memcpy` が律速）。
- **USBSS にはシステムクロックの速さが要ります。** HSI の 25 MHz のまま動かしていたときは、DMA が
  リンクに追いつかず、EP0 IN の 96 バイト目以降で 32 バイトの塊が重複していました。既定の
  400 MHz（V3F と HCLK は 100 MHz）では、512 バイトまで正しく返ります（2026-10-21）。
- **手順の出どころ:** EVT の USBSS デモと、TinyUSB PR #3779 の実測による注意点に沿っています。
  ファイル冒頭にまとめてあります。

`begin()` が失敗する理由（`lastError()`）:

| 値 | 意味 |
|---|---|
| `Unsupported` | この系列ではまだ USB デバイスが使えない |
| `PortUnavailable` | `config.controller` に、この部品に無いコントローラを指定した、または必要な HSE を使えない |
| `TooManyInstances` | 同じクラスがクラスごとの上限（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）より多い |
| `NotStarted` / `NotReady` | `begin()` の前、またはホストが構成していないときに HID のレポートを送ろうとした |
| `OutOfEndpoints` | エンドポイントが足りない（X035 は 7 本） |
| `DescriptorTooLarge` | クラスの記述子が組めない（HID のレポート記述子が解析できない、など） |
| `InvalidArgument` | 設定の値が正しくない（`deviceInterfaceGuid` の形） |

### 4. 同じクラスを2つ以上

同じクラスは、クラスごとの上限まで宣言するだけで持てます（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）。
ビルド時の設定は要りません。宣言しなかったクラスはリンクされず、宣言した分だけ RAM を使います。

バッファの大きさは、パケットの大きさ（コントローラと速度で決まる）から決めます。CDC とベンダー（`CH32RVUsbBulkStreamConfig`）、CCID
（`CH32RVUsbCcidConfig`）は `configure()` でスケッチの配列に差し替えられます。オーディオとビデオのパケットの上限は、コントローラのドライバが知らせる値
（`device.maxPacketSize()`）です。ビデオをバルクで送るかアイソクロナスで送るかは `configure()`（`CH32RVUsbVideoConfig::transport`）で選びます。

ビルド時の上限はありません。クラスの数、HID クラスの数、構成記述子・HID の合成レポート記述子・BOS・
Microsoft OS 2.0 の大きさは、どれも RAM に組み立てて置かず、ホストが読むときに EP0 のパケットごとに作るので、
決まった上限がありません（エンドポイントの数はコントローラで決まります）。識別と名前の既定値も define では
変えません（`CH32RVUsbDeviceConfig`、`setName()`、`vendorID()` などで実行時に変えます）。残っている define は、既定の
DFU ランタイムをビルドから外す `CH32RV_USB_NO_DEFAULT_DFU` だけです。

例: [SerialMulti](../libraries/CH32RVUsbDevice/examples/SerialMulti/)（CDC を2つ）

### コントローラと速度

`config.controller` で使用する物理 controller を選びます。controller と対応系列は上の一覧を参照してください。

| 値 | 意味 |
|---|---|
| `Auto` | 系列の既定 controller。接続したケーブルやコネクタの自動検出ではない |
| `FullSpeed` | USBFS / 旧 USB のポート |
| `HighSpeed` / `HighSpeed2` | USBHS のポート。2基目の有無や HSE 条件は型番による |
| `SuperSpeed` | USBSS のポート。対応する配線・ホストが必要 |
| `Usbd` | V20x の USBD。USBFS と物理端子や共有資源が異なる |

選んだ controller に対応するコネクタへ接続します。`HighSpeed` や `SuperSpeed` を指定しても、
別 controller の端子がその速度へ切り替わるわけではありません。
USB のクロックは系列のドライバが設定するため、必要な発振器とボード設定も確認します。

送れる量はコントローラで決まります。

| コントローラ | 系列 | パケット | 目安 |
|---|---|---|---|
| USBFS | X035 / X033、V103 | 64 B（アイソクロナスも 1 ms に 64 B） | オーディオは 16 bit で 32 kHz モノラル / 16 kHz ステレオまで。ビデオはバルクで小さい画像 |
| USBFS（EP3 IN だけ 1023 B） | V20x、V30x | アイソクロナスの IN は EP3 なら 1 ms に 1023 B まで。ほかのエンドポイントと OUT は 64 B（エンドポイントは宣言順に割り当て） | — |
| USBD（fsdev） | V20x | 64 B。パケット用のメモリ 512 B を CAN1 と共有 | — |
| USBHS | V305 / V307 / V317 | バルク 512 B、アイソクロナス 1024 B（125 µs ごと） | 48 kHz ステレオのオーディオ、大きめのビデオ |

## WebUSB と Windows（MS OS 2.0）

ベンダー（バルク）のインターフェースか DFU ランタイムがあると、デバイスは BOS ディスクリプタ（bcdUSB 2.10）を持ち、
次の2つを載せます。並べ方・ベンダーコード・既定の GUID は EspUsbDevice と同じなので、どちら向けに書いたホストの
プログラムも、そのまま相手を見つけられます。

- **Microsoft OS 2.0 ディスクリプタ**: Windows がベンダーと DFU のインターフェースに WinUSB をドライバのインストール
  なしで当てます。ベンダーには `DeviceInterfaceGUIDs`（ホストのアプリが探す GUID）も付けます。
  - `config.deviceInterfaceGuid`: 製品ごとに自分の GUID を付けます（`{8-4-4-4-12}` の形。違うと `begin()` が
    `InvalidArgument`）。既定は EspUsbDevice と同じ GUID です。
  - `config.msOs20Layout`: `AUTO`（インターフェースが2つ以上ならサブセット形式、1つなら平らな形式）、`FLAT`、`SUBSETS`。
  - `config.msOs20CcgpDevice`: 1機能でも複合デバイスとして扱わせる。`config.msOs20VendorRevision`: 0 なら内容から
    自動で決まり、内容が変わると Windows が読み直します。
- **WebUSB**: `config.webusbEnabled = true` と `config.webusbUrl` で、Chrome などがデバイスを開けるようになり、つないだ
  ときにそのページを案内します。

X035 の実機で、BOS・MS OS 2.0 の中身（互換 ID 2つ、GUID）・WebUSB の URL をホストから読み出して確かめています
（tests/instrumented/usb）。Windows での動作はまだ確かめていません。

## スタックの動かし方

USB の処理は、`loop()` の後・`delay()` の中・`SerialUSB` などの各関数の中で、コントローラにイベントがあるときだけ
行います（`ch32rv_usb_task()`）。

- `loop()` を長く止める（`delay()` を使わずに待つ）と、その間はホストへの応答が止まります。ホストからは、切断ではなく
  「止まったデバイス」に見えます。
- クラスの通知のコールバック（`onOutputReport`、`onLineState` など）は、この処理の中では呼ばれません。スケッチが `loop()`
  で `deliverEvents()` を呼んだときに届きます（[スケッチの組み立て方](sketch-guide.ja.md)、例:
  [SerialEvents](../libraries/CH32RVUsbDevice/examples/SerialEvents/README.ja.md)）。ホストが答えを待つ provider（MSC の
  `onRead()` / `onWrite()`、CCID の `onApdu()` など）だけは、この処理の中から呼ばれます。
- 何もしていないときの負荷（X035、`loop()` の1秒あたりの回数）: USB なし 42.8 万回、USB あり 20.7 万回です。毎回処理する
  作りだと 7.8 万回まで落ちるので、イベントがあるときだけ処理しています。

## 書き込み

USB から更新へ移る入口には、CDC の1200 bps touchと DFU_DETACH があります。
入口と実際の書き込み方式は区別します。X035/X033 の工場 ISP、H41x の自己書き換え用 DFU mode、
それ以外の系列の扱いは以下と上の対応表を確認してください。

| 入口 | スケッチ側 | ホスト側 | 費用 |
|---|---|---|---|
| 1200bps タッチ | CDC ポート（`SerialUSB` など）がある。`setBootTouchEnabled(false)` で無効 | ポートを 1200 baud で開いて閉じる | CDC に含まれる |
| DFU_DETACH | 既定で付く（`config.dfuRuntime = false` で外す） | DFU_DETACH を送る | 約 0.5 KB の Flash（CDC 構成に足して +532 B）、RAM +20 B |

```sh
# アプリのデバイスを指定すると、DFU_DETACH かタッチかを ch32rv が選んでブートローダーに入れ、書き込む
ch32rv --non-interactive isp flash sketch.ino.elf --entry auto --device /dev/ttyACM0
```

**DFU ランタイムは既定で付きます。** 更新へ移行できるかと実際の更新方式は系列の実装に依存します。
外すときは `config.dfuRuntime = false` です（インターフェースが無くなります）。実行時の設定なのでコードは残ります。
ビルドからも外すには、スケッチの `build_opt.h` に `-DCH32RV_USB_NO_DEFAULT_DFU` と書きます。

**ch32rv の discovery**: DFU ランタイムのインターフェース名が "CH32RV"（`CH32RV_USB_DFU_RUNTIME_NAME`）のデバイスは、
ch32rv の discovery が書き込み先のポート（`ch32rv-app://<USB シリアル>`、表示は「<製品名> (CH32RV, USB)」。モニタは同じデバイスの
CDC）として出します（ch32rv f20aee7）。
VID:PID ではこのコアのスケッチだと判定できないので、この名前を目印にしています。名前を変えると DFU_DETACH 自体は
動きますが、このポートには出なくなります。工場出荷のブートローダーにいる間は `ch32rv-isp://<USB の位置>` が出ます。
Arduino IDE ではこのポートを選んで「書き込み」を押すだけで、USB だけで書き込めます（ch32rv 0.20.0 以降。`isp flash --entry auto`）。
モニタは同じデバイスの CDC を開きます（CDC が無ければ開けない旨を表示）。X035 で、`ch32rv-app` ポートからの書き込みと、
従来のプローブ（`oep://`）からの書き込みの両方を確かめています。

**工場 ISP と DFU mode は別です。** X035/X033 は DFU_DETACH から工場 ISP へ移り、
実際の書き込みは WCH の ISP プロトコルを使います。この経路で `dfu-util -D` は使いません。
H41x の自己書き換えに対応した構成では、DFU_DETACH 後に DFU mode として再列挙し、
`dfu-util -D` のバイナリを flash の先頭から書き込みます。使用する画像・対象・経路は上の H41x の説明も確認します。

- 実測（X035）: DETACH またはタッチから、ブートローダーが現れるまで 0.2〜1 秒です。DETACH → `isp flash` → 新しい
  スケッチの起動、タッチ → `isp flash` → 起動の両方を、デバッグ線なしで通しました。
- **USB を使わないスケッチを書き込んだ後は、USB からは入れません。** WeAct X035 の板なら DOWNLOAD キー（PC17 = D+ を
  High）を押しながら USB をつなぐか、デバッグプローブで入れます（[書き込み経路](upload-routes.ja.md)）。
- Windows: DFU のインターフェースには MS OS 2.0 ディスクリプタで WinUSB の互換 ID を付けているので、ドライバの
  インストールなしで dfu-util から開けるはずです（Windows では未確認）。
- Linux: DETACH を送るプログラムには、そのデバイス（VID:PID）を開く権限が要ります（udev のルール）。
- ch32rv の `--entry` は `auto`（DFU ランタイムがあれば DETACH、CDC ならタッチ、どちらも無ければ案内を出して
  待つ）、`dfu-detach`、`touch1200`、`probe` です（ch32rv 69b2840 以降）。Arduino IDE の書き込みはこれを使う予定です。
- DETACH への応答は、ホスト側でよく STALL（Pipe error）になります。ch32rv は警告だけ出して、ブートローダーの
  出現を待ちます。

起動時に USB を自動で始める仕組み（スケッチが USB を使わなくても書き込める形）は、今は入れていません。
`setup()` より前に始めても認識される時間は変わらず（0.3〜0.5 秒）、USB を使わないスケッチにも費用がかかる
（Flash 約 11 KB、RAM 約 2.5 KB）ためです。必要になったら、ボードメニューで足せる形にしてあります。

## PD（CH32RVUsbPd）

USB-C の PD 充電器から、固定電圧や PPS の電圧を受け取ります（シンク、`USBPD`）。板を PD の電源にすることも
できます（ソース、`USBPDSource`。例: [PowerSource](../libraries/CH32RVUsbPd/examples/PowerSource/)）。電源の切り替えは
スケッチのコールバックで行い、実際にその電圧が出ているかはライブラリの範囲外です。シンクとソースは同時には動きません。

```cpp
#include <CH32RVUsbPd.h>

void setup() {
  if (!USBPD.begin()) {
    return;
  }
  const uint32_t start = millis();
  while (!USBPD.ready()) {
    if (millis() - start >= 5000) {
      return;                       // 未接続・PD 非対応もある
    }
  }
  USBPD.request(9000);               // 9 V。実アプリでは戻り値も確認
}
void loop() { }                     // begin() が PPS の契約を保つ
```

この要求で VBUS 自体が9 Vに上がります。電源入力回路・負荷の定格を確認し、
最初の接続は [PD サンプルの準備](../libraries/CH32RVUsbPd/examples/README.ja.md)に従ってください。

- 例: [ListProfiles](../libraries/CH32RVUsbPd/examples/ListProfiles/)（充電器が出せる電圧の一覧）、
  [CycleVoltages](../libraries/CH32RVUsbPd/examples/CycleVoltages/)（ボタンで固定電圧を切り替える）
- 詳しくは [CH32RVUsbPd の README](../libraries/CH32RVUsbPd/README.ja.md)。
- PD とデバイスは同じ USB-C で同時に使えます（CC は PC14 / PC15、データは PC16 / PC17）。ただし、データ
  通信のできる PD 電源（PD に対応した PC のポートなど）でなければ、両方が同時に役立つ場面はありません。
- 確認: シンクは固定電圧と PPS の充電器を相手に、X035 で実機確認済みです。ソースはビルドと単体試験のみで、X035 どうしの
  対向試験はまだです（ソースにする板は CC の Rd、WeAct なら R4 / R5 を外す）。名前を変えた後も、PD の無い電源（PC のポート）を
  正しく判定することを確かめています。

## 費用（X035、空のスケッチ Flash 744 B / RAM 4 B との差）

2026-10-20 に測った値です。右の列は、移行前（コミット 3ab1761、TinyUSB）の同じスケッチの値です（Flash / RAM）。

| スケッチ | Flash | RAM | TinyUSB の時 |
|---|---|---|---|
| `#include <CH32RVUsbDevice.h>` だけ | 0 | 0 | 0 / 0 |
| DFU ランタイムだけ（`USBDevice.begin()`） | +10.8 KB | +1.8 KB | +10.6 KB / +3.1 KB |
| `SerialUSB` のエコー（既定の DFU ランタイム込み） | +13.1 KB | +2.4 KB | +14.6 KB / +3.9 KB |
| キーボード1つ（US 配列、DFU 込み） | +16.0 KB | +2.1 KB | +16.8 KB / +3.7 KB |
| ベンダー（バルク）の往復（DFU 込み、BOS / MS OS 2.0 も込み） | +12.5 KB | +2.4 KB | +13.3 KB / +3.6 KB |
| MSC + 8 KB の RAM ディスク（DFU 込み。RAM はディスクの 8 KB を含む） | +14.4 KB | +10.5 KB | +15.3 KB / +11.8 KB |
| CCID（DFU 込み） | +13.4 KB | +2.6 KB | +13.4 KB / +3.9 KB |
| ネットワーク（NCM、DFU 込み） | +13.4 KB | +5.9 KB | +14.4 KB / +7.3 KB |
| オーディオ（16 kHz モノラルのマイク、DFU 込み） | +18.4 KB | +2.8 KB | +23.0 KB / +4.2 KB |
| ビデオ（UVC、生成した絵、DFU 込み） | +14.4 KB | +2.1 KB | +18.3 KB / +3.3 KB |

記述子（構成、HID の合成レポート、BOS、MS OS 2.0）は RAM に置かず、ホストが読むときに作ります。そのぶん
Flash は約 1 KB 増え、RAM は約 0.9 KB 減りました（RAM の少ない X035 に合わせた選択です）。
どのクラスを使っても1回だけかかるのは、コア（EP0・標準要求・記述子の組み立て、約 3 KB）、X035 のドライバ
（約 2.7 KB）、DFU ランタイムが連れてくる BOS / MS OS 2.0（約 1.8 KB）です。系列が届かない速度（X035 では
High / SuperSpeed）のための処理は、ビルドの時点で落ちます。キーボードのレイアウトは1種あたり約 1 KB です。

## ホスト（CH32RVUsbHost）

チップの USB ポートをホストにして、キーボード・マウス・USB シリアル・MIDI・USB メモリ・ベンダーのバルク・
スマートカードリーダーをつなぎます。API は EspUsbHost（同じ作者の ESP32 用）のものを `CH32RVUsbHost` と読み替えた形です。

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial port(usb);       // USB シリアルを Stream として

void setup() {
  Serial.begin(115200);
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  port.begin(115200);
  if (!usb.begin()) {
    Serial.println(usb.lastErrorName());
  }
}
void loop() {
  deliverEvents();                      // onKeyboard() はここで動く
  while (port.available()) Serial.write(port.read());
}
```

| 系列 | ポート | 確認 |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS（PC16 / PC17） | C8T6 で ESP32-S3（EspUsbDevice）相手に HID・CDC・MSC・MIDI・ベンダー・CCID を実機確認 |
| CH32X035 F8U6 / F7P6 / D8U6、X033 | シリコンにホストが無い（データシートの型番表）。`begin()` が `Unsupported` | F8U6 で実機確認 |
| CH32V203 / V208 | USBFS（PB6 / PB7） | ビルドのみ |
| CH32V303 / V305 / V307 / V317 | USBFS（PA11 / PA12） | ビルドのみ |

- VBUS は基板が供給します（チップには入り切りの機能がありません）。
- ホストとデバイスは同じコントローラを使うので、同時にはどちらか一方です（もう一方が動いていれば `Busy`）。
- ホストのスタックとコントローラのドライバ（`CH32RVUsbHcdUsbfs.cpp`、`CH32RVUsbHcd.h` の表）はこのコアのものです。
  開いているエンドポイントごとに転送を持ち、NAK は次のフレームで再試行します。列挙はルートポートとハブのポートを
  ノンブロッキングの状態機械で進めます。
- 詳しくは [CH32RVUsbHost の README](../libraries/CH32RVUsbHost/README.ja.md)。対向試験は
  [tests/peer/usb](../tests/peer/usb/README.ja.md)（ESP32-S3 がホストのときの X035 のデバイス機能も、ここで確かめています）。

## 今後の予定

- **デバイスのドライバの実機確認**: X035 / X033 と H417（FS・SS）以外は、ドライバはあるがビルドのみ
  （上のコントローラの表）。
- **クロック**: CPU Clock のメニューで選ぶ（2026-10-22）。L103 / M103 は既定が 8 MHz のままで、USB には 48 / 72 / 96 MHz を選ぶ（USB は未確認。72 / 96 MHz で動いている L103 には OEP probe が接続できない（WCH-LinkE はできる）。48 MHz は接続できる。試すときは [起動待ち](upload-routes.ja.md#接続できなくなるイメージを試すとき起動待ち) を付ける）。V208 は
  USB と Ethernet を同時に使えるクロックが無い（README のメニューの表）。
- **PD**: M030 の 2 つ目のブロック（USBPD1、PA2 / PA3）。X035 以外の実機確認。
- **H417**: USBHS の実機確認（SWD と端子を共有）、USBSS のホスト、V5F の利用。
- **USB ネットワークの IP 層**: lwIP の上に作った（`NetworkUsbNcm`、docs/network-stack.ja.md）。実機の NCM は未確認。
- **ほかの系列の工場出荷 ISP への入口**: `enterBootloader()`（DFU_DETACH、1200bps タッチ）は X035 / X033 で実機確認。
  X305 / X315 は同じ FLASH_STATR.BOOT_MODE があるので同じ手順を入れた（未確認。ブートローダーがどのポートで待つかも未確認）。
  H41x は BOOT_MODE があるが試したら起動に失敗した（上）。V20x / V30x / V103 / L103 / V205 / M030 / V4x7 には BOOT_MODE が
  無く、ブートローダーには BOOT0 の端子でしか入れないので、普通のリセットです。
- **ホストの X035 以外での実機確認**: X035 は C8T6 と ESP32-S3 で確認済み。V20x / V30x / V205 / L103 / M030 / H41x
  （USBOTG_FS の配置、`ch32rv_usbfs_otg.h`）はビルドのみ。
- **ホストのアイソクロナスと USB ネットワーク**（オーディオ、ビデオ、NCM）: 今は無し。

## 既知の問題

- 実機での確認範囲は上の対応表を参照してください。未確認の構成では次の点も確認が必要です。
  - USB の 48 MHz を HSI（±1.6〜2.5 %）から作っています。USB の Full Speed が求めるのは ±0.25 % です。X035 は
    HSI で動いていますが、ほかの系列で通信が不安定なら HSE を使う設定が要ります。
  - V103 の USBFS で、エンドポイント 4〜7 の自動トグルが効くか（資料は EP1〜3 とも読める）。
  - デバッガがクロックを書き換えたあと、コアが `SystemInit()` をやり直すと USB の分周（USBPRE）が消えます。
- DFU_DETACH への応答が、ホスト側でよく STALL（libusb の Pipe error）として返ります。デバイスは毎回
  ブートローダーに入るので、ホスト側は応答のエラーを無視して、ブートローダーの出現を待ってください。
- provider のコールバック（MSC の `onRead()` など）の中で長い `delay()` を呼ぶと、その間の USB 処理は止まります
  （入れ子にはしません）。通知のコールバックは `deliverEvents()` から呼ばれるので、中の `delay()` でも USB は進みます。
- **読み出し保護をかけた X035 には、USB から書き込めません**（ch32rv 側の実測、BTVER 2.60）。DFU_DETACH やタッチで
  BOOT_MODE は立ちますが、ブートローダーが Flash を読めずに止まり、ISP が現れません。また、電源投入後にデバッガが
  一度でも接続すると、アプリも止まります。解除できるのは、デバッグ線からの `ch32rv recover --method unprotect`
  （Flash の全消去）だけです。
