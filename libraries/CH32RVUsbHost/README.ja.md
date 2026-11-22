# CH32RVUsbHost — チップの USB をホストとして使う

[English](README.md)

キーボード・マウスなどの HID、USB シリアル（CDC-ACM と FTDI / CP210x / CH34x / PL2303）、MIDI、マスストレージ、
ベンダーのバルクインターフェース、スマートカードリーダー（CCID）を、ハブ越しも含めて扱います。

API は同じ作者の ESP32 用ライブラリ EspUsbHost のものを、
`EspUsbHost` → `CH32RVUsbHost`、`ESP_USB_HOST_` → `CH32RV_USB_HOST_` と読み替えた形です。

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) { usb.printDeviceInfo(d.address); });
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  if (!usb.begin()) {
    Serial.println(usb.lastErrorName());
  }
}
void loop() { deliverEvents(); }   // callback はここで動く。スタックは loop() の後と delay() の中で動く
```

## 対応

| 系列 | ポート | 確認 |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS（PC16 / PC17） | C8T6 で ESP32-S3（EspUsbDevice）相手に HID・CDC・MSC・MIDI・ベンダー・CCID を実機確認 |
| CH32X035 F8U6 / F7P6 / D8U6、CH32X033 | — （シリコンにホストが無い。`begin()` が `Unsupported`） | F8U6 で `Unsupported` を実機確認 |
| CH32V203 / V208 | USBFS（PB6 / PB7） | ビルドのみ |
| CH32V103 | USB（旧 USBFS の IP、PA11 / PA12） | ビルドのみ。CPU Clock は 48 / 72 MHz（PLL から 48 MHz を作る） |
| CH32V303 / V305 / V307 / V317 | USBFS（PA11 / PA12）。V305 / V307 / V317 は `CH32RV_USB_HOST_PORT_HIGH_SPEED` で第 1 世代の USBHS（PB6 / PB7、HSE が要る） | ビルドのみ |
| CH32V205、CH32M030、CH32H415 / H416 / H417 | USBFS（PA11 / PA12）。V205 と H41x は `CH32RV_USB_HOST_PORT_HIGH_SPEED` で USBHS（H41x は PB8 / PB9 で SWD と兼用） | ビルドのみ。48 MHz の作り方はデバイスと同じ（V205 と H41x は USBHS PLL、M030 は 144 MHz の PLL） |
| CH32H416 / H417（追加） | `CH32RV_USB_HOST_PORT_SUPER_SPEED` で USBSS。USB 3（5 Gbps）だけ | 実装済み、実機では未確認 |
| CH32V407 / V467 | USBHS1（既定、`HIGH_SPEED`）、USBHS2（`HIGH_SPEED_2`） | V407 の USBHS1 で ESP32-P4（EspUsbDevice、HS）相手に HID・CDC・MSC・MIDI・ベンダー・CCID を実機確認 |
| CH32X305 / X315 | USBHS（既定） | ビルドのみ |
| CH32L103 / M103 | USBFS（PA11 / PA12） | ビルドのみ。CPU Clock の 8 MHz では 48 MHz が作れず `begin()` が失敗する（48 / 72 / 96 MHz を選ぶ。未確認） |
| ほかの系列 | — （`begin()` が `Unsupported`） | ビルドのみ |

X035 のどの型番がホストを持つかはデータシートの型番表のとおりで、`begin()` は BASE_CTRL のホストモードのビットが
立つかどうかで実際に確かめます（F8U6 では立ちません）。対向試験は [tests/peer/usb/host](../../tests/peer/usb/host/)
にあり、CH32X035C8T6 と ESP32-S3 で両方（usb-peer-host / usb-peer-host-classes）が、V407 と ESP32-P4（HS）でも
両方が通っています。

**VBUS は基板が供給します。** チップには VBUS を入り切りする機能がありません。ホストを始めても、Type-C のソースの CC の回路や、
基板の外部電源の切り替えは設定しません。

### ポートと速さ

`begin()` は系列の既定のポートで始めます。ほかのポートは `CH32RVUsbHostConfig` で選びます（EspUsbHost の
`EspUsbHostConfig.port` と同じ形）。

```cpp
CH32RVUsbHostConfig config;
config.port = CH32RV_USB_HOST_PORT_HIGH_SPEED;   // DEFAULT / HIGH_SPEED / FULL_SPEED / HIGH_SPEED_2（V407 / V467 の USBHS2）
                                                 // / SUPER_SPEED（H416 / H417 の USBSS）
usb.begin(config);                               // 無いポートは Unsupported
```

第 2 世代の USBHS（V407 / V467、X305 / X315、V205、H41x）では、HS（480 Mbps）の機器を HS で、FS・LS の機器を
それぞれの速さで直接つなげます。`CH32RVUsbHostDeviceInfo::speed` は `CH32RV_USB_HOST_SPEED_HIGH`（2）になります。
受信の受け皿は、既定のポートが HS の系列（V407 / V467、X305 / X315）では 512 バイトのパケットが入る大きさです
（`CH32RV_USB_HOST_MAX_PACKET`、既定のリングも大きくなります）。既定のポートが FS の系列（V205、H41x、V30x）は FS の 64 バイトの
ままで、FS のポートだけのスケッチは大きくなりません。HS（と H416 / H417 の SS）のポートを選んだときだけ、そのポートのドライバが
大きいパケットのエンドポイントにヒープから受け皿を取ります（CDC・MIDI・ベンダー・CCID。SS は 1024 バイト。HID のレポートは
64 バイトまで）。

- HS には水晶（HSE）が要ります。ドライバが HSE を起こし、無い基板では HSI のままです（短いパケットしか通りません）。
- HS のハブの先の FS・LS の機器は、ハブのトランザクショントランスレーター（TT）を通る split トランザクションで扱います
  （SPLIT のトークンはコントローラーが出し、開始と完了の split はドライバが組みます）。シングル TT・マルチ TT の
  どちらのハブでも同じ形です。ハブの中で打ち切った転送の後の CLEAR_TT_BUFFER と、188 バイトを超える等時の OUT は
  ありません。**実装済みで、実機ではまだ確かめていません。**
- H41x の USBHS は PB8 / PB9 で、SWD と同じ端子です。`begin()` の後はデバッガがつながりません（リセットまで）。
- X305 / X315 は、HSE が無いと HSI をホストの SOF で合わせる仕組みしか無いので、ホストには HSE が要ります。
- OUT の後の NYET は受け付けとして扱い、PING は送りません（機器は空くまで NAK します）。

第 1 世代の USBHS（V305 / V307 / V317 の `HIGH_SPEED`、PB6 / PB7）も HS のホストです。PHY の PLL は水晶からしか
作れないので、HSE が要ります（Clock Source のメニューの水晶。HSE を選んでいないときは 8 MHz とみなします）。
**実装済みで、実機ではまだ確かめていません。** このコントローラーの SPLIT の欄は 12 ビットで中身の説明が無いため、
HS のハブの先の FS・LS の機器は扱いません（そのエンドポイントは開けません）。FS のハブの先の LS の機器（PRE）も
確かめていません。V30x のクラスの受け皿は FS の 64 バイトのままで（USBFS だけのスケッチは大きくなりません）、HS の 512 バイトの
パケットのエンドポイントには、開くときにヒープから受け皿を取ります（CDC・MIDI・ベンダー・CCID。HID のレポートは 64 バイトまで）。

どのポートでも、リンクされるのはスケッチが選んだポートのドライバだけです（`begin()` がポートを見て選ぶのは
スケッチの側で、定数のポートなら他のドライバは入りません）。

H416 / H417 の `SUPER_SPEED` は、USBSS を SuperSpeed（USB 3、5 Gbps）のホストとして動かします。**実装済みで、実機では
まだ確かめていません。** SuperSpeed だけを扱います。同じコネクターの USB 2.0 の機器や USB 3 のハブの USB 2.0 側は
USBHS（`HIGH_SPEED`）の担当で、別のホストです（両方を同時に動かすことは、まだできません）。`speed` は
`CH32RV_USB_HOST_SPEED_SUPER`（3）になり、1024 バイトのパケットのエンドポイントには、USBSS のドライバがヒープから
受け皿を取ります（`CH32RV_USB_HOST_MAX_PACKET` は FS の 64 のまま）。スタックの SuperSpeed の部分（BOS・SET_SEL などの列挙、
ルート文字列、SS のハブ、512 バイトの EP0 のための 1024 バイトの記述子の窓）も、`SUPER_SPEED` を選んだときだけリンクされます。
FS のポートだけのスケッチは、ほかの FS の系列と同じ大きさです（HID のレポート記述子も 256 バイトまで）。

- 制御・バルク・インタラプト・アイソクロナス（Mult を含む）。今は 1 トランザクションに 1 パケットです。SuperSpeed の
  ハブ（ルート文字列、ハブの段）。SET_SEL のあと、機器からの U1 / U2 を受け付けます（`-DCH32RV_USB_HOST_SS_LINK_PM=0`
  でビルドすると、リンクは U0 のままです）。
- ストリームはありません。コントローラーに Stream ID の欄が無いので、UAS のインターフェースは動きません（マスストレージの
  BOT のインターフェースは動きます）。

例の `Serial` はボードのハードの UART（115200 baud）で、ホストにつないだ USB の口ではありません。

必要な型番、D+/D−、VBUS、UART の接続とビルド手順は
[サンプル共通ガイド](examples/README.ja.md)を参照してください。
最初の接続確認には [DeviceInfo](examples/DeviceInfo/README.ja.md) を使います。

## できること

- **機器**: 接続 / 切断のコールバック、ディスクリプタ、文字列、インターフェース、エンドポイント、制御転送
  （`vendorControlTransfer` / `vendorControlIn` / `vendorControlOut`）、ハブ（1 段、ポートの状態と電源）。
- **HID**: キーボード（配列 16 種、Caps / Num / Scroll Lock の LED）、マウス（レポートディスクリプタからの配置）、
  コンシューマ / システムコントロール、ゲームパッド、ベンダーのレポート、生の入力、レポートディスクリプタ、
  出力 / フィーチャーレポート、ブート / レポートプロトコル。
- **CDC シリアル**: `CH32RVUsbHostCdcSerial`（Stream）または `sendSerial` / `onSerialData`。ボーレート、データ形式、DTR / RTS。
- **MIDI**: 受信のコールバック、ノート・CC・プログラム・プレッシャー・ピッチベンド・SysEx の送信。
- **マスストレージ**: INQUIRY、容量、REQUEST SENSE、TEST UNIT READY、ブロックの読み書き、LUN の選択。
  ファイルシステムは持ちません（ブロック単位）。
- **ベンダー（バルク）**: `vendorOpen` で開き、`vendorWrite` / `vendorRead` / `onVendorData`。
- **CCID**: 電源、ATR、APDU（応答の SW は `statusWord` に分けて返す。EspUsbHost と同じ）、エスケープ、スロットの状態、
  カードの抜き差し。
- **USB ネットワーク**: CDC-NCM と CDC-ECM の機器（USB の LAN アダプター、ESP32 や CH32RV の NCM のデバイス）を、生の
  Ethernet のフレームで（`networkOpen` / `networkWriteFrame` / `onNetworkFrame` / `networkReadFrame`）。

## EspUsbHost との違い

- コールバックは `CH32RVCallback` です。関数か、ポインター 2 つまでを取り込むラムダ（`[this]` など）を受け、ヒープを使いません（`std::function` ではありません）。1 つの出来事に 1 つで、リスナーの複数登録はありません。RAM が 20 KB のため。
- タスクの設定はありません。スタックは `loop()` の後と `delay()` の中で動き、待つ API はその間スタックを回します。
  コールバックはスケッチが呼ぶ `deliverEvents()` から呼ばれ（`delay()` や割り込みの中ではありません）、待つ API を
  呼んでもかまいません。それまでレポート・受信データ・パケットはスタックの buffer で待ち（満杯なら機器側で待たせます）、
  接続と切断は 4 件まで溜まります。
- アイソクロナス（オーディオ・ビデオ）はありません。
- USB ネットワークは、開けるのは 1 台で、NCM のブロックは 1 つに 1 フレームで送ります。IP のつなぎ込み
  （`networkAttachNetif()`）は Network ライブラリの対象の系列（Ethernet の MAC のある石）だけです。
- MSC のファイルシステムは `CH32RVUsbHostMscFS`（`EspUsbHostMscFS` と同じ形、下の「MSC」）です。ESP32 の VFS は無いので、ホストの `mscMount()` はありません。
- ベンダーのバルクと CCID は、同時に開けるのはそれぞれ1つです。
- USB シリアルの受信の大きさは EspUsbHost と同じ `setRxBufferSize(size)`（ヒープから、`begin()` の前か `end()` の後、66〜65535 バイト）と `rxBufferSize()` で変えます。ヒープを使わないときは `CH32RVUsbHostCdcSerial::setBuffer()` でスケッチの配列に差し替えます（下の「容量の設定と失敗の確認」）。
- エラーは `esp_err_t` ではなく `CH32RVUsbHostError` です。ESP32 の task / PHY / channel 設定を持ち込みません。

API の語彙は共有しますが、現行 EspUsbHost のすべての機能との互換を意味しません。
このリポジトリの [公開ヘッダ](src/CH32RVUsbHost.h) が利用できる API の正本です。

## ライフサイクルと callback

| API | 意味 |
|---|---|
| `begin()` / `end()` | controller と Host stack の開始/停止。戻り値と `lastErrorName()` を確認 |
| `ready()` | Host が開始している。周辺機器の列挙完了を意味しない |
| `task()` | Host の処理を明示的に進める。長い計算ループで使用 |
| `onDeviceConnected()` / `onDeviceDisconnected()` | 機器の接続/切断を通知 |
| `deviceCount()` / `getDevices()` / `getDevice()` | 接続機器と address を取得 |

callback はそれぞれ1枠の関数ポインタです。再登録すると置き換わります。キャプチャしないラムダや通常関数を使い、
必要な状態は静的変数等へ保持します。イベント中の `data` / `rawData` や文字列へのポインタを後で使う場合は、
自前の buffer へコピーしてください。

`CH32RV_USB_HOST_ANY_ADDRESS` と `CH32RV_USB_HOST_ANY_PORT` は最初の該当機器/ポートを選ぶ指定です。
USB address は抜き挿しで変わり得ます。複数機器を扱うときは接続 callback の address で選び、切断時に状態を破棄します。
CDC の port は同一デバイス内の0始まりの番号です。

### サスペンドとレジューム

| API | 意味 |
|---|---|
| `suspend()` | ルートポートを止める（SOF なし。SuperSpeed は U3）。そこの機器とハブの先も止まる。構成でリモートウェイクアップを宣言している機器には、先に有効にする（`SET_FEATURE(DEVICE_REMOTE_WAKEUP)`） |
| `resume()` | ポートが resume を出し（USB 2.0 は K を 20 ms）、機器の 10 ms の復帰を待ってから転送を再開する |
| `suspended()` | `suspend()` が効いている |
| `onRemoteWakeup()` | 機器がバスを起こした。ポートはもう戻っている |

止めている間に頼んだ転送は、ポートが戻るまで待ちます（制御転送はタイムアウトになります）。機器がないと `suspend()` は
`NotConnected`、controller のドライバが止められないと `Unsupported` で失敗します。V205 の USBFS と RP2040、V407 の
USBHS と ESP32-P4 で確かめています（tests/peer/usb の usb-peer-host-power）。V30x の USBHS と USBSS はビルドだけです。

## ディスクリプタと制御転送

| API | 用途 |
|---|---|
| `printDeviceInfo(address)` / `printAllDeviceInfo()` | identity、構成、interface、endpoint を `Print` へ出す（既定 `Serial`） |
| `getInterfaces()` / `getEndpoints()` | descriptor から interface / endpoint を列挙 |
| `getStringDescriptor(address, index, out, capacity)` | 文字列を取得。非 ASCII は `?` に変換 |
| `vendorControlTransfer(requestType, request, value, index, ...)` | request type と recipient を含めて EP0 転送 |
| `vendorControlIn()` / `vendorControlOut()` | vendor device request の簡易形 |
| `getHubInfo()` / `getHubPortStatus()` / `setHubPortPower()` | hub の情報、port status/change、port power |

独自の interface request では descriptor の interface 番号を `wIndex` に入れ、正しい recipient と方向を指定します。
USB address、interface 番号、endpoint address、CDC port番号は別の値です。`0x81` のような endpoint address は
向きを含みます。PC側アプリや別実装の例の固定値をそのまま流用せず、実際の descriptor を確認します。

hub の power API は hub の port power request であり、ボード自身の VBUS スイッチを制御する API ではありません。
hub に個別電源制御の実装がない場合もあります。

## クラス別 API

### HID

| API | 用途 |
|---|---|
| `onKeyboard()` / `setKeyboardLayout()` / `setKeyboardLeds()` | キー入力、文字配列、Lock LED |
| `onMouse()` / `onGamepad()` | descriptor を解析した入力 |
| `onConsumerControl()` / `onSystemControl()` | メディア・システム usage |
| `onHIDInput()` / `onHIDVendorInput()` / `onHIDReportDescriptor()` | 生入力、vendor入力、report descriptor |
| `sendHIDReport()` / `getHIDReport()` | interface・type・report ID を指定する report 転送 |
| `sendHIDVendorOutput()` / `sendHIDVendorFeature()` | vendor report の送信 |
| `sendSetProtocol()` | boot/report protocol の切替 |

キーボードの `ascii=0` は、そのキーが ASCII 文字にならない場合にも使います。raw usage と pressed/released を
使えば文字以外のキーも処理できます。ゲームパッドの独自配置は、生の report と descriptor で確認します。

### USB シリアル

`CH32RVUsbHostCdcSerial port(usb)` は Arduino `Stream` です。`begin(baud)`、`available()`、`read()`、`peek()`、
`write()`、`flush()` を使え、`setBaudRate()` / `setConfig()` / `setDtr()` / `setRts()` で設定します。
接続前の `begin()` は設定を保持し、機器が現れたときに適用します。複数ポートは `setAddress()` / `setPort()` で選びます。

callback 形式なら `onSerialData()` / `sendSerial()`、状態は `serialReady()` / `serialPortCount()` /
`getSerialPortInfo()` で確認します。USB の baud 設定は対向 UART の line coding で、FS バスの速度とは別です。
大量転送では `write()` の戻り値を確認して再送位置を進めます。

受信リングが埋まると、この実装は次のパケットを受け取りません（機器は NAK で待ちます）。Esp 側の「古いデータを捨てる」説明を
そのまま適用しないでください。最終的な損失の有無は対向機器・UART・読み出し頻度にも依存します。

### MIDI

`onMidiMessage()` で cable、status、data1/data2 を受けます。`midiSendNoteOn/Off()`、`midiSendControlChange()`、
program/pressure/pitch bend/SysEx の helper を使えます。channel は0始まりです。詳細な引数は
[公開ヘッダ](src/CH32RVUsbHost.h)と [MIDI サンプル](examples/MIDI/README.ja.md)を参照してください。

### MSC

`mscWaitReady()` → `mscInquiry()` / `mscCapacity()` → `mscReadBlocks()` / `mscWriteBlocks()` の順で扱います。
必要な byte 数は block count × block size です。媒体が512-byte sectorとは限らないので容量情報を確認します。
`mscRequestSense()` で SCSI の状態、`mscSelectLun()` で対象 LUN を選べます。
媒体の入っていないリーダーは、TEST UNIT READY を数回試したあと容量無しでマウントされます。`mscWaitReady()` で長く待てます。
`mscEject()` は LoEj 付きの START STOP UNIT を送ります（ファームウェア更新のドライブは、これを書き込みの終わりと受け取ります）。
`mscSynchronizeCache()` は SYNCHRONIZE CACHE(10) を送ります。

ファイルとして読み書きするには、`CH32RVUsbHostMscFS`（`#include <CH32RVUsbHostMscFS.h>`、FS ライブラリの `FatFS`）を使います。
EspUsbHost の `EspUsbHostMscFS` と同じ形です:

```cpp
#include <CH32RVUsbHost.h>
#include <CH32RVUsbHostMscFS.h>

CH32RVUsbHost usb;
CH32RVUsbHostMscFS stick;

void loop() {
  if (usb.mscReady() && !stick.mounted()) {
    stick.begin(usb);                       // "/usb"、最初のマスストレージ、LUN 0
  }
  if (stick.mounted()) {
    File f = stick.open("/log.txt", FILE_APPEND);
    f.println(millis());
    f.close();
  }
  delay(1000);
}
```

- FAT12/16/32 で、ブロックが 512 バイトの媒体だけです（exFAT は FS ライブラリで有効な系列だけ）。
- `begin(host, basePath, address, lun, maxFiles, timeoutMs, skipSyncCache)` の `basePath` と `maxFiles` は
  受け取るだけです。パスはボリュームの中のもの（`"/log.txt"`）で、同時に開けるファイルの数は FS ライブラリの
  `FS_MAX_OPEN_FILES` です。
- `sync()` と `end()` は SYNCHRONIZE CACHE を送ります（`skipSyncCache` で送らない）。キャッシュの無いドライブが
  断っても失敗にしません。
- ボリュームの無い媒体は、`stick.format(stick.device())` で作れます（format が入っている系列）。
- `CH32RVUsbHostMscFS.h` はヘッダーだけで、インクルードしないスケッチには、これも FS ライブラリも入りません。
- 抜かれたあとは、`end()` してから `begin()` し直すまで、どの呼び出しも false です。

### Vendor bulk と CCID

`vendorOpen()` で class `0xff` の interface を開き、`vendorWrite()`、`vendorRead()` / `onVendorData()` で通信します。
`getVendorInterface()` で実際の endpoint を確認できます。bulk の byte列にアプリケーションのメッセージ境界はありません。

CCID は `ccidOpen()`、`ccidPowerOn()`、`ccidApdu()`、`ccidEscape()`、カード状態通知を用意します。
`ccidApdu()` の response は SW1/SW2 を除いた payload、status word は別の出力です。ただし受信 buffer の capacity は
SW の2 bytesも収まるよう確保します。通信成功と `SW=9000` の命令成功を分けて扱います。

### USB ネットワーク（CDC-NCM / CDC-ECM）

```cpp
usb.onNetworkFrame([](const CH32RVUsbHostNetworkFrame &f) { /* f.data, f.length: Ethernet のフレーム */ });
if (usb.networkOpen()) {                  // 最初のネットワークの機器
    uint8_t frame[60] = { /* 宛先 MAC、送り元 MAC、EtherType、中身 */ };
    usb.networkWriteFrame(frame, sizeof frame);
}
```

`networkOpen()` は構成の中から通信のインターフェース（class 0x02、subclass 0x0D が NCM、0x06 が ECM）と、
そのデータのインターフェースの alternate 1 を探します。NCM では `GET_NTB_PARAMETERS` でブロックの大きさを読み、
受けるブロックが `CH32RV_USB_HOST_NETWORK_NTB_IN_MAX`（既定 3200）より大きい機器には `SET_NTB_INPUT_SIZE` で小さくして
もらいます。パケットフィルター（自分宛て・ブロードキャスト・全マルチキャスト）を設定し、`SET_INTERFACE` で alternate 1 に
するとデータが流れ始めます。機器の MAC アドレスは `getNetworkInterface()` の `macAddress`（機器の文字列から）、リンクは
`networkLinkUp()`（機器の NETWORK_CONNECTION の通知）です。

受けたフレームは、リング（`CH32RV_USB_HOST_NETWORK_RX_RING_SIZE`、既定 4096 バイト）で待ち、`onNetworkFrame()`
（`deliverEvents()` から）か、コールバックが無ければ `networkReadFrame()` で取り出します。リングに 1 ブロックぶんの空きが
無い間は受信を止め、機器を待たせます。送信は 1 フレームずつ同期で、ブロックの終わりがパケットの境目に重なるときは、
NCM では 1 バイト足し、ECM では長さ 0 のパケットを続けます。バッファ（受けのブロック、送りのブロック、リング、取り出し用）は
`networkOpen()` のときにヒープから取ります（既定で約 9 KB）。数は `networkStats()` で見られます。

その上で IP を動かすには、EspUsbHost と同じく `networkAttachNetif()` を呼びます（`#include <NetworkUsbHost.h>`、
Network ライブラリ）。既定は DHCP のクライアントで、`NetworkClient` / `NetworkUDP` / DNS が USB の機器を通ります。
インターフェースは `networkInterface()`（`NetworkInterface`: `hasIP()`、`localIP()`、`printTo()`）、アドレスは
`networkLocalIP()` です。この間フレームはインターフェースのもので、`onNetworkFrame()` / `networkReadFrame()` は使いません。
例は Network ライブラリの [UsbHostNetwork](../Network/examples/UsbHostNetwork/README.ja.md) です。V407 と ESP32-P4
（EspUsbDevice の NCM）の組で、生のフレームの往復、DHCP、TCP の往復を確かめています（2026-11-22、usb-peer-host-net）。

## 容量の設定と失敗の確認

持てる数は決まっています: デバイス 5（ハブとその先）、ハブ 2、HID インタフェース 4、USB シリアル 2 ポート、
MSC 1、MIDI 1。構成記述子は長さを問わず（USB の上限の 65535 バイトまで）読みます。256 バイト（SuperSpeed の EP0 が
512 バイトの H416 / H417 では 1024）の窓で、先頭から読み直して窓より前を捨てるので、機器に合わせて RAM が増えることは
ありません。クラスドライバには自分のインタフェースから窓の分が渡り、1 つの機能の記述子（インタフェース・クラス・
エンドポイントの記述子の合計）は約 190 バイト（H416 / H417 は約 510）まで読めます。長い構成では、列挙の制御転送が少し増えます。

受信バッファは既定の大きさを持ち、スケッチの配列に差し替えられます（ビルド時の設定はありません）。

| 既定 | 対象 | 差し替え |
|---|---|---|
| 128 | USB シリアル（Stream）の受信リング | `setRxBufferSize()` / `CH32RVUsbHostCdcSerial::setBuffer()` |
| 256 | ベンダーの受信リング | `setVendorBuffer()`（`vendorOpen()` の前） |
| 272 | CCID の受信リングと応答 | `setCcidBuffers()`（`ccidOpen()` の前） |

HID インタフェースごとに覚えるレポート ID は 8 です。製造者・製品・シリアル番号の文字列は切らずに持ちます（記述子が
運べる 126 文字まで。`CH32RV_USB_HOST_STRING_MAX` は終端込みの 127）。置き場所は機器で共有する 1 つの領域で、512 バイト
（高速のホストのある系列は 1024）。1 台の 3 つの文字列が最長でも入り、ほかの機器の普通の短い文字列も入る大きさです。
領域が埋まっているときだけ、文字列は入る分に切られます。

| エラー | 意味 |
|---|---|
| `Unsupported` | Host 未実装、またはシリコンに Host がない |
| `Busy` | controller を Device が使用中 |
| `NotStarted` / `NotConnected` | 開始前 / 対象機器なし |
| `NotSupported` | 必要な機能がない、または interface が未 open |
| `Timeout` / `Stalled` / `TransferFailed` | 時間切れ / 相手の拒否 / 転送失敗 |
| `InvalidArgument` / `InvalidResponse` | 引数 / 応答が不正 |
| `NoMemory` | buffer / 管理表の容量不足 |

## サンプル

配線・実行・出力・制約は[サンプル一覧](examples/README.ja.md)から個別の README を開いてください。

| 例 | 確認すること |
|---|---|
| [DeviceInfo](examples/DeviceInfo/README.ja.md) | 列挙、interface/endpoint、抜き挿し |
| [Keyboard](examples/Keyboard/README.ja.md) / [Mouse](examples/Mouse/README.ja.md) | HID 入力と LED |
| [USBSerial](examples/USBSerial/README.ja.md) | UART と USB シリアルの中継 |
| [MIDI](examples/MIDI/README.ja.md) | MIDI 入力と周期 Note |
| [MSCBlockDump](examples/MSCBlockDump/README.ja.md) | 媒体の容量と LBA 0の読み出し |
| [VendorBulk](examples/VendorBulk/README.ja.md) | 既知の bulk protocol の送受信 |
| [CcidReader](examples/CcidReader/README.ja.md) | ATR と APDU status word |

## 中身

- ホストスタックはこのライブラリ自身のものです（`CH32RVUsbHost.cpp`。TinyUSB は使いません）。割り込みは転送の
  完了とポートの変化を記録するだけで、制御転送の段階・列挙（ルートポートとハブ）・クラスの処理は通常の文脈で
  動きます。ホストコントローラのドライバ（`CH32RVUsbHcdUsbfs.cpp`、`internal/CH32RVUsbHcd.h` の表）は、開いているエンドポイントごとに転送を持ち、1 本しかない
  トランザクションエンジンで順に流します。NAK は次のフレーム（インタラプトは bInterval ごと）で再試行し、STALL・
  無応答・トグル違いを区別します。制御転送は 5 秒（USB 仕様の上限）で打ち切ります。
- スケッチが使う機能のファイルだけがリンクされます（ハブはいつも入ります）。
- デバイス（CH32RVUsbDevice）とは同じコントローラを使うので、同時にはどちらか一方です（もう一方が動いていれば `Busy`）。
