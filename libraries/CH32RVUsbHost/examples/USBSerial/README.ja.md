# USBSerial

English: [README.md](README.md)

CDC-ACM または対応 USB-UART アダプタを接続し、ボードの UART モニタと双方向に中継します。

## 必要なものと配線

CDC-ACM / FTDI / CP210x / CH34x / PL2303 の機器、Host の VBUS、115200 bps UART モニタ。USB-UART アダプタのループバックを試すならその TX/RX を接続します。

## 操作と期待する結果

1. UART モニタに接続機器の `connected:` が出ることを確認します。
2. モニタから送った文字は USB シリアル機器へ送られます。
3. 対向機器からの文字はモニタへ表示されます。アダプタの TX/RX ループバックなら送った文字が戻ります。

## 設定と制約

`Serial` はログ/UART、`port` は Host が開く USB シリアルです。`port.begin(115200)` は接続前でも設定を保持します。既定では最初に見つかった対応ポートを使い、`setAddress()` / `setPort()` で指定できます。

Esp 側の `setRxBufferSize()` はありません。受信リングはビルド時の `CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE`（既定128 bytes）、TinyUSB FIFO は `CFG_TUH_CDC_RX_BUFSIZE` 等で変更します。`write()` の戻り値を扱う必要がある大量転送には、この1-byte中継を拡張してください。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[USBSerial.ino](USBSerial.ino)
