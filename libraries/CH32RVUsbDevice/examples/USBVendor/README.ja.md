# USBVendor

English: [README.md](README.md)

HID ではない vendor-specific interface に bulk IN/OUT と EP0 vendor request を用意します。WebUSB と WinUSB 用の descriptor も有効にします。

## 必要なものと配線

PC の libusb/WinUSB/WebUSB 対応アプリ、または別ボードの Host VendorBulk サンプル。UART ログは115200 bpsです。

## 操作と期待する結果

1. interface class `0xff` の vendor interface を探して開きます。endpoint番号は descriptor から取得します。
2. bulk OUT に `hello` を送ると、bulk IN に `echo: hello` と CRLF が返ります。
3. 約3秒ごとに `status rx=... control=...` も bulk IN に届きます。
4. vendor IN request `0x01` は `CH32RVUsbDeviceVendor` を返します。vendor OUT request `0x02` はデータなしの status 応答用です。

## 設定と制約

受信 callback は最大64 bytesずつ読み、その単位で `echo: ` を付けて返します。USB bulk にアプリの行・メッセージ境界はありません。IN は echo と周期 status が混在するため、最初の read が必ず echo とは限りません。

`webusbUrl` はサンプル文字列で、アプリを提供する URL ではありません。WebUSB を実際に使う場合は、自分の Web アプリとブラウザ側のデバイス選択/許可処理が必要です。COM ポート用の端末ではこの bulk interface を開けません。Linux はアクセス権、Windows は WinUSB バインドも確認します。

## PC から PyUSB で試す

Python の PyUSB と libusb backend を用意し、デバイスを開くアクセス権を設定した環境で実行します。
同じ VID/PID の機器を複数接続している場合は、serial number 等で選択条件を追加してください。
interface/endpoint を descriptor から選ぶので、DFU runtime の interface を誤って使うことを避けられます。

```python
import usb.core
import usb.util

dev = usb.core.find(idVendor=0x1209, idProduct=0x0001)
if dev is None:
    raise RuntimeError("USBVendor device not found")
cfg = dev.get_active_configuration()
itf = next(i for i in cfg if i.bInterfaceClass == 0xff)
ep_in = next(e for e in itf if e.bEndpointAddress & 0x80)
ep_out = next(e for e in itf if not e.bEndpointAddress & 0x80)

usb.util.claim_interface(dev, itf.bInterfaceNumber)
try:
    info = dev.ctrl_transfer(0xc1, 0x01, 0, itf.bInterfaceNumber, 64)
    print(bytes(info).decode("ascii"))  # CH32RVUsbDeviceVendor
    ep_out.write(b"hello", timeout=2000)
    print(bytes(ep_in.read(512, timeout=2000)))  # echo または周期 status
    dev.ctrl_transfer(0x41, 0x02, 0, itf.bInterfaceNumber, b"")
finally:
    usb.util.release_interface(dev, itf.bInterfaceNumber)
    usb.util.dispose_resources(dev)
```

`0xc1` / `0x41` は vendor・interface recipient の IN / OUT です。権限エラーや
interface claim 失敗はまずホスト側の権限・driver binding・ほかのアプリによる使用を確認します。
複数の status が溜まっていれば、追加で IN を読み、echo を取り出します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `onRx()` / `read(buffer, size)` | 到着通知を `deliverEvents()` で受け、実際に取得した長さだけ処理します。 |
| `write(buffer, size)` | 送信 FIFO が受け付けたバイト数を返します。CDC と同じ待ち方と仮定せず、部分書き込みを扱います。 |
| `flush()` / `waitWritable(bytes, timeoutMs)` | 短いデータの送出を促し、必要なら空きを期限付きで待ちます。受信データの消去ではありません。 |
| `onControlRequest()` / `sendControlResponse()` | EP0 の vendor 要求へ応答します。これは通知ではなく USB 処理中の provider です。 |

この例のエコーは `write()` の戻り値をすべて確認していません。大きな返信にする場合は、送れた分だけ位置を進め、
未送信分を次の `loop()` へ残します。相手が IN を読まない場合も考え、タイムアウト時の扱いを決めます。

制御要求を追加する場合は方向・recipient・`bRequest`・`wIndex`・`wLength` を検査し、対応しない要求には
`false` を返します。データ応答は SETUP で準備し、後続 stage と重複して副作用を実行しない形にします。
要求 `0x02` はこの例ではデータなしです。可変長 OUT payload を扱う実装としてそのまま流用しません。

### コピーを減らす直接送信へ変更する場合

`writeDirect(buffer, length)` は呼び出し側の配列を直接参照する送信です。通常の `write()` と違い、
成功して戻っても配列を再利用できません。`onTxComplete()` を `deliverEvents()` で受けるまで、
その配列を破棄・上書きせずに保持します。失敗理由は `lastDirectErrorName()` で確認します。

ローカル配列を渡してすぐ関数から戻る使い方は避けます。通常の送信 FIFO と直接送信を無計画に混在させず、
次のデータを投入できない場合の保留・破棄方針を決めます。実際の応用例は
[HsAdcStream](../../../CH32RVHsAdc/examples/HsAdcStream/README.ja.md) と
[LogicStream](../../../CH32RVLogicCapture/examples/LogicStream/README.ja.md) を参照してください。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[USBVendor.ino](USBVendor.ino)
