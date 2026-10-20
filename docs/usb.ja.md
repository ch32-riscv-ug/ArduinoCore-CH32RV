# USB の使い方（デバイス・ホスト・PD）

このコアの USB は、次の4つのライブラリで構成します。デバイスはこのコア自身のスタック、ホストは TinyUSB です（[ADR-0012](adr/0012-usb-stack.ja.md)。ホストも自前に移す予定です）。

| ライブラリ | 役割 | スケッチから |
|---|---|---|
| [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/) | チップの USB をデバイスとして使う（CDC・HID・MIDI・ベンダー・MSC・CCID・ネットワーク・オーディオ・ビデオ・DFU ランタイム） | `#include <CH32RVUsbDevice.h>` |
| [CH32RVUsbPd](../libraries/CH32RVUsbPd/) | USB-C の PD 充電器に電圧を頼む（シンク）、PD で電圧を出す（ソース） | `#include <CH32RVUsbPd.h>` |
| [CH32RVUsbHost](../libraries/CH32RVUsbHost/) | チップの USB をホストとして使う（HID・CDC シリアル・MIDI・MSC・ベンダー・CCID） | `#include <CH32RVUsbHost.h>` |
| [CH32RVTinyUSB](../libraries/CH32RVTinyUSB/) | 同梱の TinyUSB 本体と設定。今はホストだけが使う（ホストコントローラのドライバはこのコアのもの） | 直接は使わない |

ホストは [CH32RVUsbHost](../libraries/CH32RVUsbHost/)（`#include <CH32RVUsbHost.h>`）です（[ホスト](#ホストch32rvusbhost)）。

## サンプルから始める

各サンプルフォルダに日本語と英語の README があり、必要な機器、配線、操作、期待するログ、設定値と制約を記載しています。
共通の配線とビルド方法を確認してから、個別の例へ進んでください。

| 目的 | 最初の例 | 全サンプルと接続手順 |
|---|---|---|
| ボードを PC の USB Device にする | [SerialUSBEcho](../libraries/CH32RVUsbDevice/examples/SerialUSBEcho/README.ja.md) | [Device 共通ガイドと27例](../libraries/CH32RVUsbDevice/examples/README.ja.md) |
| ボードへ USB 周辺機器を接続する | [DeviceInfo](../libraries/CH32RVUsbHost/examples/DeviceInfo/README.ja.md) | [Host 共通ガイドと8例](../libraries/CH32RVUsbHost/examples/README.ja.md) |
| PD 充電器/電源を扱う | [ListProfiles](../libraries/CH32RVUsbPd/examples/ListProfiles/README.ja.md) | [PD 共通ガイドと3例](../libraries/CH32RVUsbPd/examples/README.ja.md) |

`Serial` はハードウェア UART、`SerialUSB` / Device の `UsbSerial` は USB CDC、Host の `CH32RVUsbHostCdcSerial` は
接続した USB シリアル機器への窓口です。ログを見る端子と、テストする USB データ端子を区別します。
Device では WeAct X035F8U6、Host では X035C8T6 を実機基準にしており、同じ X035 でも Host の有無が異なります。

## 対応状況

| 系列 | デバイス | PD | 確認 |
|---|---|---|---|
| CH32X035 / X033 | 動く（USBFS） | シンクとソース | X035（WeAct CH32X035F8U6 の板）で実機確認（PD ソースはビルドのみ） |
| ほかの系列 | `begin()` が `false`（`Unsupported`） | `begin()` が `false` | ビルドのみ |

デバイスは自前のスタックに移したところで、コントローラのドライバ（`CH32RVUsbDcd.h` の表）は X035 / X033 の USBFS
だけです。V103、V20x（USBFS・USBD）、V30x（USBFS・USBHS）は TinyUSB の時代には動いていた（ビルドのみ）ので、
そのクロックと PHY の立ち上げ（コミット 3ab1761 の `CH32RVUsbDevicePort.h`）を使ってドライバを足していきます。
ディスクリプタとクラスは Full / High / SuperSpeed のどれでも組めるようにしてあります。

`begin()` が失敗する理由（`lastError()`）:

| 値 | 意味 |
|---|---|
| `Unsupported` | この系列ではまだ USB デバイスが使えない |
| `PortUnavailable` | `config.controller` に、この部品に無いコントローラを指定した、または必要な HSE を使えない |
| `TooManyFunctions` | クラスが `CH32RV_USB_MAX_FUNCTIONS`（既定 6）より多い |
| `TooManyInstances` | 同じクラスがクラスごとの上限（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）より多い |
| `NotStarted` / `NotReady` | `begin()` の前、またはホストが構成していないときに HID のレポートを送ろうとした |
| `OutOfEndpoints` | エンドポイントが足りない（X035 は 7 本） |
| `DescriptorTooLarge` | 構成ディスクリプタが `CH32RV_USB_CONFIG_MAX`（既定 512 B）を超えた |
| `InvalidArgument` | 設定の値が正しくない（`deviceInterfaceGuid` の形） |

### 4. 同じクラスを2つ以上

同じクラスは、クラスごとの上限まで宣言するだけで持てます（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）。
ビルド時の設定は要りません。宣言しなかったクラスはリンクされず、宣言した分だけ RAM を使います。

バッファの大きさは、パケットの大きさ（コントローラと速度で決まる）から決めます。CDC は `setBuffers()` で
スケッチの配列に差し替えられます。オーディオとビデオのパケットの上限は、コントローラのドライバが知らせる値
（`device.maxPacketSize()`）です。ビデオをバルクで送るかアイソクロナスで送るかは `setTransferType()` で選びます。

残っている define（スケッチのフォルダーの `build_opt.h` で変えられる。[ADR-0007](adr/0007-user-build-option-injection.ja.md)）:

| 設定 | 既定 | 意味 |
|---|---|---|
| `CH32RV_USB_MAX_HID_CLASSES` | 6 | 1つのデバイスに宣言できる HID クラスの数 |
| `CH32RV_USB_HID_REPORT_MAX` | 288 | HID を合成したレポートディスクリプタの大きさ（RAM） |
| `CH32RV_USB_DEVICE_CCID_BUFFER_SIZE` | 280 | CCID のメッセージの最大長（受信と送信に1つずつ） |
| `CH32RV_USB_MAX_FUNCTIONS` | 6 | デバイスに付けられるクラスの数 |
| `CH32RV_USB_CONFIG_MAX` | 512 | 構成ディスクリプタの組み立て用バッファ（RAM） |

例: [SerialMulti](../libraries/CH32RVUsbDevice/examples/SerialMulti/)（CDC を2つ）

### コントローラと速度

`config.controller` でどの USB を使うかを選びます。今ドライバがあるのは X035 / X033 の USBFS（`Auto` / `FullSpeed`）
だけで、下の表のほかの値と系列は、ドライバを足したときの形です。

| 値 | 意味 |
|---|---|
| `Auto`（既定） | その系列の標準。V305 / V307 / V317 は USBFS（HS のピンしか無い V305FBP6 / GBU6 は USBHS）。V203 / V208 は USBFS（USBD しか無い V203F6P6 / G6U6 は USBD） |
| `FullSpeed` | USBFS |
| `HighSpeed` | USBHS（V305 / V307 / V317）。HS の PHY は HSE（水晶）が要る。周波数は `CH32RV_HSE_HZ`（既定 8 MHz、`build_opt.h` で変更）。水晶が立たなければ `begin()` が `PortUnavailable` |
| `Usbd` | V20x の USBD（ST の fsdev 互換、PA11 / PA12。パケットメモリと割り込みを CAN1 と共用） |

ディスクリプタは速度に合わせて作り直します（HS ではバルク 512 B、割り込みの間隔はマイクロフレーム単位、
device qualifier と other speed configuration も返す）。システムクロックは HSI の PLL のままで、USB の
48 MHz はそこから分周します。

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
- クラスのコールバック（`onOutputReport`、`onLineState` など）は、この処理の中から呼ばれます。割り込みからではありません。
- 何もしていないときの負荷（X035、`loop()` の1秒あたりの回数）: USB なし 42.8 万回、USB あり 20.7 万回です。毎回処理する
  作りだと 7.8 万回まで落ちるので、イベントがあるときだけ処理しています。

## 書き込み

USB だけで書き込み直す入口は2つあります。どちらも、チップを工場出荷の USB ブートローダー（1a86:55e0）でリセット
し、`ch32rv isp` で書き込みます。アップロードが無ければ、約 10 秒でスケッチに戻ります。

| 入口 | スケッチ側 | ホスト側 | 費用 |
|---|---|---|---|
| 1200bps タッチ | CDC ポート（`SerialUSB` など）がある。`setBootTouchEnabled(false)` で無効 | ポートを 1200 baud で開いて閉じる | CDC に含まれる |
| DFU_DETACH | 既定で付く（`config.dfuRuntime = false` で外す） | DFU_DETACH を送る | 約 0.5 KB の Flash（CDC 構成に足して +532 B）、RAM +20 B |

```sh
# アプリのデバイスを指定すると、DFU_DETACH かタッチかを ch32rv が選んでブートローダーに入れ、書き込む
ch32rv --non-interactive isp flash sketch.ino.elf --entry auto --app-device port:/dev/ttyACM0
```

**DFU ランタイムは既定で付きます。** USB デバイスを使うスケッチなら、何も書かなくても USB から書き込み直せます。
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

**DFU は入口だけです。** `CH32RVUsbDeviceDfuRuntime` は DFU のランタイムインターフェース（DFU_DETACH を受ける
だけのもの）で、DFU モードもダウンロードもありません。ディスクリプタでも書き込みができる（CAN_DOWNLOAD）とは
名乗っていません。現れるのは工場出荷の ISP ブートローダーで、書き込みは WCH の ISP プロトコルです。dfu-util では
書けません（`dfu-util -e` で ISP に入れることはできます）。

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
void loop() { USBPD.maintain(); }    // PPS の契約を保つ
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

## 費用（X035、空のスケッチ Flash 728 B / RAM 4 B との差）

**下の値は TinyUSB の時代のものです。自前のスタックで測り直します。**

| スケッチ | Flash | RAM |
|---|---|---|
| `#include <CH32RVUsbDevice.h>` だけ | 0 | 0 |
| `SerialUSB`（既定の DFU ランタイム込み） | +12.4 KB | +3.2 KB |
| キーボード1つ（US 配列、DFU ランタイム込み） | +14.6 KB | +3.0 KB |
| CDC + DFU + HID 6種（合成）+ 2ケーブルの MIDI、日本語配列 | +23.1 KB | +4.3 KB |
| DFU ランタイムだけ（`USBDevice.begin()`） | +10.5 KB | +3.4 KB |
| ベンダー（バルク）の往復（DFU 込み、BOS / MS OS 2.0 も込み） | +13.1 KB | +3.9 KB |
| MSC + 8 KB の RAM ディスク（DFU 込み。RAM はディスクの 8 KB を含む） | +15.1 KB | +12.1 KB |
| 同じ大きさの FAT RAM ディスク（`format()`・`addTextFile()`） | +16.9 KB | +12.1 KB |
| CCID（DFU 込み） | +13.3 KB | +4.2 KB |
| ネットワーク（NCM、DFU 込み） | +14.9 KB | +7.5 KB |
| オーディオ（マイク・スピーカー・ヘッドセットのどれか、DFU 込み） | 約 +30 KB | 約 +4.4 KB |
| ビデオ（UVC、DFU 込み） | 約 +25.6 KB | 約 +4.1 KB |

V305 / V307 / V317 は USBFS と USBHS の、V203 / V208 は USBFS と USBD の両方のドライバが入るので、同じスケッチで数 KB 多くなります（V30x で約 3.6 KB）。

内訳の大半は、TinyUSB のデバイス層（約 3.7 KB）とコントローラのドライバ（約 2.2 KB / 1.3 KB）で、どのクラスを使っても
1回だけかかります。キーボードのレイアウトは1種あたり約 1 KB です。

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
void loop() { while (port.available()) Serial.write(port.read()); }
```

| 系列 | ポート | 確認 |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS（PC16 / PC17） | C8T6 で ESP32-S3（EspUsbDevice）相手に HID・CDC・MSC・MIDI・ベンダー・CCID を実機確認 |
| CH32X035 F8U6 / F7P6 / D8U6、X033 | シリコンにホストが無い（データシートの型番表）。`begin()` が `Unsupported` | F8U6 で実機確認 |
| CH32V203 / V208 | USBFS（PB6 / PB7） | ビルドのみ |
| CH32V303 / V305 / V307 / V317 | USBFS（PA11 / PA12） | ビルドのみ |

- VBUS は基板が供給します（チップには入り切りの機能がありません）。
- ホストとデバイスは同じコントローラを使うので、同時にはどちらか一方です（もう一方が動いていれば `Busy`）。
- ホストコントローラのドライバは、同梱の TinyUSB の V20x 用（ビジー待ちで転送が1本ずつ）ではなく、このコアで
  書いたもの（`ch32rv_hcd_usbfs.c`）です。開いているエンドポイントごとに転送を持ち、NAK は次のフレームで再試行します。
- 詳しくは [CH32RVUsbHost の README](../libraries/CH32RVUsbHost/README.ja.md)。対向試験は
  [tests/peer/usb](../tests/peer/usb/README.ja.md)（ESP32-S3 がホストのときの X035 のデバイス機能も、ここで確かめています）。

## 今後の予定

- **デバイスのコントローラのドライバ**: V103 の USBFS、V20x の USBFS と USBD、V30x の USBFS と USBHS（TinyUSB の
  時代の立ち上げはコミット 3ab1761）。
- **ホストを自前のスタックに移す**（デバイスと同じ形）。
- **残りの系列**:
  - L103 / M103、M030、V205: USB のコントローラは今の USBFS の仕組みで動きますが、ボードの既定クロックが
    8 MHz（PLL なし）で、USB を動かすには既定のクロックを上げる必要があります（方針の確認待ち）。
  - V407 / V467、X305 / X315、V205 の USBHS: V307 とは別の「第2世代」の USBHS で、ドライバを新しく書く必要が
    あります。H417 の USBSS（SuperSpeed）はその後。
- **ベンダーの直接転送**（`writeDirect()`）: USBHS の DMA で入れる。今は `NotSupported`。
- **USB ネットワークの IP 層**（EspUsbDevice の `ipConfig()` / `dhcpServer()` / `beginNetwork()`）: IP スタック
  （lwIP など）が要るので、入れるかは方針の確認待ち。
- **ほかの系列の工場出荷 ISP への入口**: `enterBootloader()`（DFU_DETACH、1200bps タッチ）は今 X035 / X033 だけが
  ブートローダーに入ります。ほかの系列は普通のリセットです。
- **ホストの V20x / V30x での実機確認**: X035 は C8T6 と ESP32-S3 で確認済み。V20x / V30x（レジスタ配置が別）は
  ビルドのみ。
- **準備できない MSC でデバイス全体が止まる**（ホスト）: TinyUSB の MSC ホストは列挙中の TEST UNIT READY を上限なく
  繰り返すため、メディアの無いカードリーダーのような MSC をつなぐと、同じデバイスの HID や CDC もマウント
  されない（upstream の TODO。回数か時間で打ち切る修正を入れるか検討）。
- **ホストのアイソクロナスと USB ネットワーク**（オーディオ、ビデオ、NCM）: 今は無し。

## 既知の問題

- **X035 以外の系列は実機で未確認です。** 次の3点は、ドライバを足して実機で確かめるまで不確かです。
  - USB の 48 MHz を HSI（±1.6〜2.5 %）から作っています。USB の Full Speed が求めるのは ±0.25 % です。X035 は
    HSI で動いていますが、ほかの系列で通信が不安定なら HSE を使う設定が要ります。
  - V103 の USBFS で、エンドポイント 4〜7 の自動トグルが効くか（資料は EP1〜3 とも読める）。
  - デバッガがクロックを書き換えたあと、コアが `SystemInit()` をやり直すと USB の分周（USBPRE）が消えます。
- DFU_DETACH への応答が、ホスト側でよく STALL（libusb の Pipe error）として返ります。デバイスは毎回
  ブートローダーに入るので、ホスト側は応答のエラーを無視して、ブートローダーの出現を待ってください。
- Arduino の慣例どおり `delay()` の中でも USB を処理するので、USB のコールバックの中で長い `delay()` を呼ぶと、その
  間の USB 処理は止まります（入れ子にはしません）。
- **読み出し保護をかけた X035 には、USB から書き込めません**（ch32rv 側の実測、BTVER 2.60）。DFU_DETACH やタッチで
  BOOT_MODE は立ちますが、ブートローダーが Flash を読めずに止まり、ISP が現れません。また、電源投入後にデバッガが
  一度でも接続すると、アプリも止まります。解除できるのは、デバッグ線からの `ch32rv recover --method unprotect`
  （Flash の全消去）だけです。
