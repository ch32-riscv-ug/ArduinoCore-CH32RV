# USB の使い方（デバイス・PD）

このコアの USB は、次の3つのライブラリで構成します。スタックは TinyUSB です（[ADR-0012](adr/0012-usb-stack.ja.md)）。

| ライブラリ | 役割 | スケッチから |
|---|---|---|
| [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/) | チップの USB をデバイスとして使う（CDC・HID・MIDI・DFU ランタイム） | `#include <CH32RVUsbDevice.h>` |
| [CH32RVUsbPd](../libraries/CH32RVUsbPd/) | USB-C の PD 充電器に電圧を頼む（シンク） | `#include <CH32RVUsbPd.h>` |
| [CH32RVTinyUSB](../libraries/CH32RVTinyUSB/) | 同梱の TinyUSB 本体と設定 | 直接は使わない |

ホスト（`CH32RVUsbHost`）はまだありません（[今後の予定](#今後の予定)）。

## 対応状況

| 系列 | デバイス | PD | 確認 |
|---|---|---|---|
| CH32X035 / X033 | 動く（USBFS） | 動く（シンク） | X035（WeAct CH32X035F8U6 の板）で実機確認 |
| ほかの系列 | `begin()` が `false`（`Unsupported`） | `begin()` が `false` | ビルドのみ |

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
CH32RVUsbDeviceKeyboard keyboard;      // 定義済みの USBDevice に付く
CH32RVUsbDeviceMidi midi;

void setup() { SerialUSB.begin(); }    // CDC + キーボード + MIDI の複合デバイスになる
```

| クラス | 内容 | 例 |
|---|---|---|
| `CH32RVUsbDeviceCdcSerial` | 仮想 COM ポート（`Stream`）。`SerialUSB` はこれ | SerialUSBEcho、TwoSerialPorts |
| `CH32RVUsbDeviceKeyboard` | ブートキーボード。`print()` で文字を打つ | Keyboard |
| `CH32RVUsbDeviceMouse` | ブートマウス | Mouse |
| `CH32RVUsbDeviceHidGeneric` | ベンダー定義の HID。ドライバ不要のデータ通信（hidapi、WebHID） | HidGenericEcho |
| `CH32RVUsbDeviceHid` | 任意のレポートディスクリプタの HID | — |
| `CH32RVUsbDeviceMidi` | USB MIDI（ノート、CC、生パケット） | MidiController |
| `CH32RVUsbDeviceDfuRuntime` | USB だけで書き込み直す入口（[書き込み](#書き込み) を参照） | DfuRuntime |

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

- `serialNumber` を省略すると、チップの固有 ID（64 bit）を使います。同じ PC に2枚つないでも区別できます。
- インターフェースの名前は `setName()` で変えられます（`console.setName("Console")`）。
- 1209:0001 は pid.codes のテスト用 ID です。机の上の試作には使えますが、製品には使えません。

例: [CustomDevice](../libraries/CH32RVUsbDevice/examples/CustomDevice/CustomDevice.ino)

`begin()` が失敗する理由（`lastError()`）:

| 値 | 意味 |
|---|---|
| `Unsupported` | この系列ではまだ USB デバイスが使えない |
| `PortUnavailable` | `config.port` に、この部品に無いコントローラを指定した |
| `TooManyFunctions` | クラスが `CH32RV_USB_MAX_FUNCTIONS`（既定 4）より多い |
| `TooManyInstances` | 同じクラスが上限（`CFG_TUD_CDC` など、既定 1）より多い |
| `OutOfEndpoints` | エンドポイントが足りない（X035 は 7 本） |
| `DescriptorTooLarge` | 構成ディスクリプタが `CH32RV_USB_CONFIG_MAX`（既定 256 B）を超えた |

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
| `CFG_TUD_CDC` / `CFG_TUD_HID` / `CFG_TUD_MIDI` / `CFG_TUD_DFU_RUNTIME` | 1 | そのクラスをいくつまで持てるか |
| `CFG_TUD_CDC_RX_BUFSIZE` / `CFG_TUD_CDC_TX_BUFSIZE` | 256 | CDC 1ポートあたりのバッファ |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | HID のレポートの最大長 |
| `CFG_TUD_MIDI_RX_BUFSIZE` / `CFG_TUD_MIDI_TX_BUFSIZE` | 64 | MIDI のバッファ |
| `CH32RV_USB_MAX_FUNCTIONS` | 4 | デバイスに付けられるクラスの数 |
| `CH32RV_USB_CONFIG_MAX` | 256 | 構成ディスクリプタの組み立て用バッファ（RAM） |

上限を上げても、そのクラスを宣言しなければ費用はかかりません。ただし宣言した場合は、上限の数だけ RAM を確保します
（CDC 1つあたり約 0.7 KB）。

例: [TwoSerialPorts](../libraries/CH32RVUsbDevice/examples/TwoSerialPorts/)（`build_opt.h` で CDC を2つ）

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
| DFU ランタイム | `CH32RVUsbDeviceDfuRuntime` を宣言 | DFU_DETACH を送る（`dfu-util -e` など） | CDC 構成に足して Flash +532 B、RAM +20 B |

```sh
# X035 / X033: DETACH またはタッチでブートローダーに入れ、出てきたら書き込む
ch32rv isp flash sketch.ino.elf --wait 10
```

- 実測（X035）: DETACH またはタッチから、ブートローダーが現れるまで 0.2〜1 秒です。DETACH → `isp flash` → 新しい
  スケッチの起動、タッチ → `isp flash` → 起動の両方を、デバッグ線なしで通しました。
- **USB を使わないスケッチを書き込んだ後は、USB からは入れません。** WeAct X035 の板なら DOWNLOAD キー（PC17 = D+ を
  High）を押しながら USB をつなぐか、デバッグプローブで入れます（[書き込み経路](upload-routes.ja.md)）。
- Windows: DFU のインターフェースには MS OS 2.0 ディスクリプタで WinUSB の互換 ID を付けているので、ドライバの
  インストールなしで dfu-util から開けるはずです（Windows では未確認）。
- Linux: DETACH を送るプログラムには、そのデバイス（VID:PID）を開く権限が要ります（udev のルール）。
- ch32rv 側では、`isp flash --entry auto --app-device port:...`（DETACH かタッチかを自動で選ぶ入口）を準備中です。
  Arduino IDE の書き込みはそれを使う予定です。

起動時に USB を自動で始める仕組み（スケッチが USB を使わなくても書き込める形）は、今は入れていません。
`setup()` より前に始めても認識される時間は変わらず（0.3〜0.5 秒）、USB を使わないスケッチにも費用がかかる
（Flash 約 11 KB、RAM 約 2.5 KB）ためです。必要になったら、ボードメニューで足せる形にしてあります。

## PD（CH32RVUsbPd）

USB-C の PD 充電器から、固定電圧や PPS の電圧を受け取ります（シンク）。

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
- 確認: 固定電圧と PPS の充電器を相手に、X035 で実機確認済みです。名前を変えた後も、PD の無い電源（PC のポート）を
  正しく判定することを確かめています。

## 費用（X035、空のスケッチ Flash 728 B / RAM 4 B との差）

| スケッチ | Flash | RAM |
|---|---|---|
| `#include <CH32RVUsbDevice.h>` だけ | 0 | 0 |
| `SerialUSB` | +12.0 KB | +3.0 KB |
| `CH32RVUsbDeviceDfuRuntime` だけ | +8.8 KB | +2.3 KB |
| `SerialUSB` + DFU ランタイム | +12.6 KB | +3.1 KB |
| CDC + DFU + HID + MIDI（全部入り） | +16.5 KB | +3.7 KB |

内訳の大半は、TinyUSB のデバイス層（約 3.7 KB）とコントローラのドライバ（約 2.2 KB / 1.3 KB）で、どのクラスを使っても
1回だけかかります。

## 今後の予定

- **ホスト（`CH32RVUsbHost`）**: EspUsbHost と同じく、ホスト本体にクラスのオブジェクトを付ける形にします。同梱の
  TinyUSB のホストドライバは CH32V20x 用しかないので、V20x から始めます。X035 などはドライバから書く必要があります。
- **コントローラを2つ持つ系列（V30x の FS / HS、V20x の USBD / USBFS）**: `config.port` で実行時に選べるように
  します。両方のドライバを名前を変えて入れ、rhport で振り分ける方法で、V307 でリンクまで確認済みです（Flash +2 KB）。
  実機確認がまだです。
- **ほかの系列のデバイス対応**: V20x、V30x、L103 など。PHY とクロックの初期化をこのライブラリに足す必要があります。

## 既知の問題

- DFU_DETACH への応答が、ホスト側で時々 STALL（libusb の Pipe error）として返ります（十数回中数回）。デバイスは毎回
  ブートローダーに入るので、ホスト側は応答のエラーを無視して、ブートローダーの出現を待ってください。
- Arduino の慣例どおり `delay()` の中でも USB を処理するので、USB のコールバックの中で長い `delay()` を呼ぶと、その
  間の USB 処理は止まります（入れ子にはしません）。
