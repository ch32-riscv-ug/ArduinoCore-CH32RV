# USB の使い方（デバイス・PD）

このコアの USB は、次の4つのライブラリで構成します。スタックは TinyUSB です（[ADR-0012](adr/0012-usb-stack.ja.md)）。

| ライブラリ | 役割 | スケッチから |
|---|---|---|
| [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/) | チップの USB をデバイスとして使う（CDC・HID・MIDI・ベンダー・MSC・CCID・ネットワーク・オーディオ・ビデオ・DFU ランタイム） | `#include <CH32RVUsbDevice.h>` |
| [CH32RVUsbPd](../libraries/CH32RVUsbPd/) | USB-C の PD 充電器に電圧を頼む（シンク）、PD で電圧を出す（ソース） | `#include <CH32RVUsbPd.h>` |
| [CH32RVUsbHost](../libraries/CH32RVUsbHost/) | チップの USB をホストとして使う（HID・CDC シリアル・MIDI・MSC・ベンダー・CCID） | `#include <CH32RVUsbHost.h>` |
| [CH32RVTinyUSB](../libraries/CH32RVTinyUSB/) | 同梱の TinyUSB 本体と設定（ホストコントローラのドライバはこのコアのもの） | 直接は使わない |

ホストは [CH32RVUsbHost](../libraries/CH32RVUsbHost/)（`#include <CH32RVUsbHost.h>`）です（[ホスト](#ホストch32rvusbhost)）。

## 対応状況

| 系列 | デバイス | PD | 確認 |
|---|---|---|---|
| CH32X035 / X033 | 動く（USBFS） | シンクとソース | X035（WeAct CH32X035F8U6 の板）で実機確認。ホストを PC と ESP32-S3（EspUsbHost）の両方にして全クラスを確認（PD ソースはビルドのみ） |
| CH32V103 | USBFS（PA11 / PA12） | — | ビルドのみ |
| CH32V203 / V208 | USBFS（PB6 / PB7）と USBD（PA11 / PA12）を `config.controller` で選ぶ。`Auto` は USBFS（USBD しか無い V203F6P6 / G6U6 は USBD。USBFS しか無い F8P6 / F8U6 で `Usbd` は `PortUnavailable`） | — | ビルドのみ |
| CH32V303 | USBFS（PA11 / PA12） | — | ビルドのみ |
| CH32V305 / V307 / V317 | USBFS（PA11 / PA12）と USBHS（PB6 / PB7、High Speed、HSE が要る）を `config.controller` で選ぶ。`Auto` は USBFS（USBHS しか無い V305FBP6 / GBU6 は USBHS） | — | ビルドのみ |
| ほかの系列 | `begin()` が `false`（`Unsupported`） | `begin()` が `false` | ビルドのみ |

「ビルドのみ」の系列は、ベンチの DUT の USB 端子がまだ配線されていないため、実機では確かめていません。

系列ごとのほかのペリフェラルとまとめた表は [peripheral-support.ja.md](peripheral-support.ja.md) にあります。

USB の無い系列でも、同じスケッチはビルドできます。動かないことは `begin()` の戻り値で分かります。

## 使い始め方

### 1. いちばん簡単な形：`SerialUSB`

`SerialUSB` は `Serial` と同じく定義済みです。`begin()` だけで、既定の名前（1209:0001、"CH32RV Arduino"）の
デバイスが CDC ポート1つで動き出します。

```cpp
#include <CH32RVUsbDevice.h>

void setup() { SerialUSB.begin(); }
void loop()  { if (SerialUSB) SerialUSB.println("hello"); delay(1000); }
```

- `if (SerialUSB)` は、ホストがポートを開いている（DTR が立っている）間だけ真になります。
- ホストが開いていないときの書き込みは捨てます。ケーブルが無くてもスケッチは止まりません。

例: [SerialUSBEcho](../libraries/CH32RVUsbDevice/examples/SerialUSBEcho/SerialUSBEcho.ino)

### 2. 機能を足す：宣言するだけ

キーボードや MIDI などの機能（クラス）は、オブジェクトを宣言するとデバイスに付きます。宣言した順に
インターフェースが並び、`begin()` のときにディスクリプタが組み立てられます。

```cpp
CH32RVUsbDeviceHidKeyboard keyboard;   // 定義済みの USBDevice に付く
CH32RVUsbDeviceMidi midi;

void setup() { SerialUSB.begin(); }    // CDC + キーボード + MIDI の複合デバイスになる
```

API は [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) に合わせてあり、`Esp` を `CH32RV` に置き換えた
名前になっています（`EspUsbDeviceHidKeyboard` → `CH32RVUsbDeviceHidKeyboard`、`ESP_USB_HID_KEY_A` →
`CH32RV_USB_HID_KEY_A`）。違いは [CH32RVUsbDevice の README](../libraries/CH32RVUsbDevice/README.ja.md#espusbdevice-からの置き換え) にまとめています。

| クラス | 内容 | 例 |
|---|---|---|
| `CH32RVUsbDeviceCdcSerial` | 仮想 COM ポート（`Stream`）。`SerialUSB` はこれ | Serial、SerialMulti、SerialUSBEcho |
| `CH32RVUsbDeviceHidKeyboard` | キーボード。19 種のレイアウト（`setLayout()`）、NKRO、Lock LED | Keyboard、KeyboardNKRO |
| `CH32RVUsbDeviceHidMouse` | マウス | Mouse、KeyboardMouse |
| `CH32RVUsbDeviceHidGamepad` | ゲームパッド（6 軸、ハット、32 ボタン） | Gamepad |
| `CH32RVUsbDeviceHidConsumerControl` / `HidSystemControl` | メディアキー / 電源・スタンバイ | MediaKeys |
| `CH32RVUsbDeviceHidVendor` | ベンダー定義の HID。ドライバ不要のデータ通信（hidapi、WebHID） | VendorHID |
| `CH32RVUsbDeviceHidCustom` | 自分のレポートディスクリプタの HID | CustomHID |
| `CH32RVUsbDeviceMidi` | USB MIDI（最大 16 ケーブル） | MIDI、MIDIController、MIDIInterface |
| `CH32RVUsbDeviceVendor` | ベンダー定義のバルク転送（libusb、WebUSB、WinUSB からドライバ不要で使う）。`onControlRequest()` でベンダーリクエストにも答えられる | USBVendor |
| `CH32RVUsbDeviceMsc` | USB メモリ（マスストレージ）。ブロックは `onRead()` / `onWrite()` か、下の RAM ディスクから | MSC、MSCFatRamDisk、CompositeHidCdcMsc |
| `CH32RVUsbDeviceMscRamDisk` / `MscFatRamDisk` | RAM 上のブロック / FAT12 のボリューム（スケッチからファイルを置く・ホストが置いたファイルを読む） | MSC、MSCFatRamDisk |
| `CH32RVUsbDeviceCcid` | スマートカードリーダー（スロット1つ）。ホストの PC/SC から見えるカードの中身をスケッチが答える | SmartCardReader |
| `CH32RVUsbDeviceMscSdCard` | SPI の SD カードを MSC の中身にする（`#include <CH32RVUsbDeviceMscSdCard.h>`。ファイルシステムのライブラリは不要） | MSCSdCard |
| `CH32RVUsbDeviceNet` | USB ネットワーク（CDC-NCM）。生の Ethernet フレームを送受信する（IP スタックは無い） | UsbNetworkPing |
| `CH32RVUsbAudioFunction` | USB オーディオ（UAC1 / UAC2）。再生・録音・両方、ミュートと音量 | AudioSpeaker、AudioMicrophone、AudioHeadset |
| `CH32RVUsbDeviceVideo` | USB カメラ（UVC）。YUY2 / MJPEG、バルクかアイソクロナス。フレームをペイロードごとに作ることもできる | VideoCamera |
| `CH32RVUsbDeviceDfuRuntime` | USB だけで書き込み直す入口。既定で付くので、宣言するのは `onDetach()` を使うときだけ（[書き込み](#書き込み) を参照） | DfuRuntime |

HID のクラスは、いくつ宣言しても1つの HID インターフェースにまとまります。1つだけならそのまま（キーボードとマウスは
BIOS でも使えるブートデバイス）、2つ以上ならレポート ID（キーボード 1、マウス 2、ゲームパッド 3、メディアキー 4、
システム 5、ベンダー 6）で区別します。

キーボードレイアウトは、`setLayout()` で名前を挙げたものだけがリンクされます（1種あたり約 1 KB）。

**宣言しなかったクラスはリンクされません。** `#include` しただけのスケッチは、Flash も RAM も増えません。

### 3. 自分のデバイスにする：設定と失敗の確認

製品名や VID/PID は、`CH32RVUsbDeviceConfig` で `begin()` に渡します。デバイス本体を自分で宣言することもできます
（1つのスケッチにデバイスは1つです）。

```cpp
CH32RVUsbDevice usb;
CH32RVUsbDeviceCdcSerial console(usb);
CH32RVUsbDeviceDfuRuntime dfu(usb);

void setup() {
  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209; config.pid = 0x0001;       // 製品には自分の ID を取る
  config.manufacturer = "Example Labs";
  config.product = "My device";
  if (!usb.begin(config)) {
    Serial.println(usb.lastErrorName());          // 理由: Unsupported、TooManyInstances など
  }
}
```

- `serialNumber` を省略すると、チップの固有 ID（64 bit、16 桁の16進）を使います。**チップごとに一意であることを仕様とします。**
  同じ PC に2枚つないでも区別でき、ch32rv の discovery はこれを書き込みポートの名前（`ch32rv-app://<シリアル>`）に
  使うので、別の USB ポートに挿し直しても IDE のポート名が変わりません。スケッチが固定のシリアルを付けて複数台を
  つないだ場合、ch32rv は重なったものを USB の位置の名前（`ch32rv-app://usb-<位置>`）に落とします。
- インターフェースの名前は `setName()` で変えられます（`console.setName("Console")`）。
- 1209:0001 は pid.codes のテスト用 ID です。机の上の試作には使えますが、製品には使えません。
- スケッチがオブジェクトを持たない2つのインターフェースの名前は、設定で変えます: `config.hidInterfaceName`（HID を
  まとめたインターフェース）、`config.dfuRuntimeName`（既定の DFU ランタイム。変える前に [書き込み](#書き込み) の
  discovery の説明を読んでください）。

**既定の文字列と ID は、すべて `build_opt.h` で上書きできます。** スケッチを書き換えずに板や製品ごとに変えたいときに
使います（実行時の設定や `setName()` があれば、そちらが優先です）。

| define | 既定 | 何の既定か |
|---|---|---|
| `CH32RV_USB_DEVICE_VID` / `_PID` / `CH32RV_USB_DEVICE_VERSION` | 0x1209 / 0x0001 / 0x0100 | `config.vid` / `pid` / `deviceVersion` |
| `CH32RV_USB_DEVICE_MANUFACTURER` / `_PRODUCT` | "ch32-riscv-ug" / "CH32RV Arduino" | `config.manufacturer` / `product` |
| `CH32RV_USB_CDC_NAME`、`_HID_NAME`、`_MIDI_NAME`、`_VENDOR_NAME`、`_MSC_NAME`、`_CCID_NAME`、`_NET_NAME`、`_AUDIO_NAME`、`_VIDEO_NAME` | "CDC"、"HID"、"MIDI"、"Vendor"、"Mass storage"、"Smart card reader"、"Network"、"Audio"、"Camera" | 各インターフェースの名前（iInterface） |
| `CH32RV_USB_DFU_RUNTIME_NAME` | "CH32RV" | DFU ランタイムの名前。ch32rv の discovery の目印 |
| `CH32RV_USB_MSC_VENDOR` / `_PRODUCT` / `_REVISION` | "CH32RV" / "MSC" / "1.0" | MSC の SCSI INQUIRY（`vendorID()` などで変更） |
| `CH32RV_USB_MSC_VOLUME_LABEL` | "CH32RV" | FAT RAM ディスクの `format()` のボリュームラベル |

文字列の define は `-DCH32RV_USB_DEVICE_PRODUCT='"My gadget"'` のように引用符ごと書きます。

例: [CustomDevice](../libraries/CH32RVUsbDevice/examples/CustomDevice/CustomDevice.ino)

`begin()` が失敗する理由（`lastError()`）:

| 値 | 意味 |
|---|---|
| `Unsupported` | この系列ではまだ USB デバイスが使えない |
| `PortUnavailable` | `config.port` に、この部品に無いコントローラを指定した |
| `TooManyFunctions` | クラスが `CH32RV_USB_MAX_FUNCTIONS`（既定 6）より多い |
| `TooManyInstances` | 同じクラスが上限（`CFG_TUD_CDC` など、既定 1）より多い |
| `NotStarted` / `NotReady` | `begin()` の前、またはホストが構成していないときに HID のレポートを送ろうとした |
| `OutOfEndpoints` | エンドポイントが足りない（X035 は 7 本） |
| `DescriptorTooLarge` | 構成ディスクリプタが `CH32RV_USB_CONFIG_MAX`（既定 512 B）を超えた |
| `InvalidArgument` | 設定の値が正しくない（`deviceInterfaceGuid` の形） |

### 4. 同じクラスを2つ以上：`build_opt.h`

同じクラスをいくつ持てるか、バッファの大きさはいくつかは、USB スタックのビルド時に決まります。既定は小さい部品に
合わせて少なめです（各クラス1つ、CDC のバッファは送受信 256 B）。スケッチと同じフォルダーに `build_opt.h` を置くと、
そのスケッチだけ変えられます（[ADR-0007](adr/0007-user-build-option-injection.ja.md)）。

```
-DCFG_TUD_CDC=2
-DCFG_TUD_CDC_RX_BUFSIZE=1024
```

| 設定 | 既定 | 意味 |
|---|---|---|
| `CFG_TUD_CDC` / `CFG_TUD_MIDI` / `CFG_TUD_VENDOR` / `CFG_TUD_DFU_RUNTIME` | 1 | そのクラスをいくつまで持てるか（HID は常に1つのインターフェースにまとまるので増やす必要はない） |
| `CH32RV_USB_MAX_HID_CLASSES` | 6 | 1つのデバイスに宣言できる HID クラスの数 |
| `CH32RV_USB_HID_REPORT_MAX` | 288 | HID を合成したレポートディスクリプタの大きさ（RAM） |
| `CFG_TUD_CDC_RX_BUFSIZE` / `CFG_TUD_CDC_TX_BUFSIZE` | 256 | CDC 1ポートあたりのバッファ |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | HID のレポートの最大長 |
| `CFG_TUD_MIDI_RX_BUFSIZE` / `CFG_TUD_MIDI_TX_BUFSIZE` | 64 | MIDI のバッファ |
| `CFG_TUD_VENDOR_RX_BUFSIZE` / `CFG_TUD_VENDOR_TX_BUFSIZE` | 128 | ベンダー（バルク）のバッファ |
| `CFG_TUD_NCM_IN_NTB_MAX_SIZE` / `CFG_TUD_NCM_OUT_NTB_MAX_SIZE` | 2048 | ネットワークの転送ブロック（仕様の最小） |
| `CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX` / `_OUT_SZ_MAX` ほか | FS 64 / HS 196 | オーディオのパケットとバッファ（tusb_config.h の audio 節） |
| `CFG_TUD_VIDEO_STREAMING_BULK` / `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` | 1 / FS 64 | ビデオをバルクで送るか、ペイロードの大きさ |
| `CFG_TUD_MSC_EP_BUFSIZE` | 512 | MSC の転送バッファ（1ブロック） |
| `CH32RV_USB_DEVICE_CCID_BUFFER_SIZE` | 280 | CCID のメッセージの最大長（受信と送信に1つずつ） |
| `CH32RV_USB_MAX_FUNCTIONS` | 6 | デバイスに付けられるクラスの数 |
| `CH32RV_USB_CONFIG_MAX` | 512 | 構成ディスクリプタの組み立て用バッファ（RAM） |

上限を上げても、そのクラスを宣言しなければ費用はかかりません。ただし宣言した場合は、上限の数だけ RAM を確保します
（CDC 1つあたり約 0.7 KB）。

例: [SerialMulti](../libraries/CH32RVUsbDevice/examples/SerialMulti/)（`build_opt.h` で CDC を2つ）

### コントローラと速度

`config.controller` でどの USB を使うかを選びます。

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
  if (USBPD.begin()) {
    while (!USBPD.ready()) { }       // 充電器との交渉を待つ
    USBPD.request(9000);             // 9 V（ミリボルト）
  }
}
void loop() { USBPD.maintain(); }    // PPS の契約を保つ
```

- 例: [ListProfiles](../libraries/CH32RVUsbPd/examples/ListProfiles/)（充電器が出せる電圧の一覧）、
  [CycleVoltages](../libraries/CH32RVUsbPd/examples/CycleVoltages/)（ボタンで固定電圧を切り替える）
- 詳しくは [CH32RVUsbPd の README](../libraries/CH32RVUsbPd/README.ja.md)。
- PD とデバイスは同じ USB-C で同時に使えます（CC は PC14 / PC15、データは PC16 / PC17）。ただし、データ
  通信のできる PD 電源（PD に対応した PC のポートなど）でなければ、両方が同時に役立つ場面はありません。
- 確認: シンクは固定電圧と PPS の充電器を相手に、X035 で実機確認済みです。ソースはビルドと単体試験のみで、X035 どうしの
  対向試験はまだです（ソースにする板は CC の Rd、WeAct なら R4 / R5 を外す）。名前を変えた後も、PD の無い電源（PC のポート）を
  正しく判定することを確かめています。

## 費用（X035、空のスケッチ Flash 728 B / RAM 4 B との差）

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
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  port.begin(115200);
  usb.begin();
}
void loop() { while (port.available()) Serial.write(port.read()); }
```

| 系列 | ポート | 確認 |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS（PC16 / PC17） | ビルドのみ |
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
- **ホストの実機確認**: ホスト機能を持つ X035（R8T6 / C8T6 / G8U6 / G8R6）か V20x / V30x を ESP32-S3 につなぎ、
  tests/peer/usb/host を流す。ベンチの X035 は F8U6 でホストを持たない。
- **ホストのアイソクロナスと USB ネットワーク**（オーディオ、ビデオ、NCM）: 今は無し。

## 既知の問題

- **X035 以外の系列は実機で未確認です。** 次の3点は実機で確かめるまで不確かです。
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
