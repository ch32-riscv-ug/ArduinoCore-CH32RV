# ADR-0012: USB stack はこのコアの自作

- Status: Accepted

## 決定

USB device（`CH32RVUsbDevice`）と USB host（`CH32RVUsbHost`）は、どちらもこのコアの自作の stack で動かします。WCH EVT の USB API も TinyUSB も使いません。PD は `CH32RVUsbPd` です（[docs/usb.ja.md](../usb.ja.md)）。API の形は EspUsbDevice / EspUsbHost に合わせ、ライブラリ名とクラス名をそろえます（[usb-espusbdevice-v3.ja.md](../usb-espusbdevice-v3.ja.md)）。

- **文脈の分け方**: 割り込みでは、SETUP の取り込みと、終わった転送の記録だけを行います。要求の処理、descriptor、クラスの処理は通常の文脈（system service、[ADR-0016](0016-execution-model.ja.md)）で、`loop()` の後と `delay()` の中で行います。スケッチの callback は `deliverEvents()` から呼びます。
- **controller**: driver の表で抽象化し、系列ごとに 1 つずつ足します（device は `CH32RVUsbDcd.h`、host は `CH32RVUsbHcd.h`）。
- **descriptor**: device は、クラスのオブジェクトを宣言で付け、`begin(config)` で組み立てます。Full / High / SuperSpeed を、速度の引数で組み分けます。宣言しないクラスはリンクされません。
- **大きさ**: build 時の define は、クラスの個数や buffer には使いません。クラスごとの上限、`setBuffers()`、driver が知らせる packet の上限（`maxPacketSize()`）で決めます。残る define は、識別の既定の文字列と、合成 buffer の上限だけです。
- **host の列挙**: root port と hub の port を、non-blocking の状態機械で進めます。MSC の TEST UNIT READY は、回数で打ち切ります（ほかの interface の mount を止めないため）。
- **起動**: USB を起動時に自動では始めません。`setup()` より前に始めても認識の時間は変わらず（0.3〜0.5 s）、USB を使わないスケッチにも費用（X035 で Flash 約 11 KB、RAM 約 2.5 KB）がかかるためです。
- **USB からの書き込み**: 入口は 1200bps touch（CDC）と DFU_DETACH（DFU runtime）の 2 つです。ch32rv の `isp flash --entry auto`（X03x）と `boot dfu flash`（H41x）が扱います（[upload-routes.ja.md](../upload-routes.ja.md)）。

## 理由

- 宣言で付けて `begin()` で descriptor を組む形では、TinyUSB の class driver をほとんど使わず、patch と build 時の define（個数・buffer）が多く残っていました。X035 では、同じ CDC + HID + MIDI + DFU で、自作のほうが Flash 約 13 KB、RAM 約 2.6 KB 小さくなりました。
- CherryUSB は X035 の port が無く、callback を割り込みの中で呼ぶので、文脈の分け方と合いません。
- 系列ごとの EVT の USB API を直接使うと、Arduino 側の API と保守が系列ごとに分かれます。

## 出所

USB serial chip（CH34x、CP210x、FTDI など）の host の手順は、TinyUSB の `cdc_host.c`（MIT）を書き直したものです。出所は該当ファイルに記します。
