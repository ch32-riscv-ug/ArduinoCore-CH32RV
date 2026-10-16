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

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[USBVendor.ino](USBVendor.ino)
