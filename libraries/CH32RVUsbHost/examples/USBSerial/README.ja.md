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

Esp 側の `setRxBufferSize()` の代わりに、`port.setBuffer(配列, 大きさ)` でスケッチの配列を受信リングにできます。`write()` の戻り値を扱う必要がある大量転送には、この1-byte中継を拡張してください。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbHostCdcSerial port(usb)` | Host 側の USB シリアルを Arduino の `Stream` として扱います。 |
| `port.begin(baud)` | 対向ポートへ適用する通信条件を設定します。USB-UART ではその先の UART 速度にも関わります。 |
| `port.available()` / `port.read()` | USB シリアルから受信したデータを読みます。`read()` は空なら `-1` です。 |
| `port.write()` | USB シリアルへ送信します。戻り値は書けたバイト数です。 |
| `setAddress()` / `setPort()` | 複数機器・複数ポートのどれを使うか明示します。OS の COM 番号ではありません。 |

`Serial` と `port` の baud は別の設定です。片方だけ変更してももう片方は変わりません。
USB-UART の先に接続する機器の電圧と通信条件も合わせます。CDC 機器なら DTR 等の制御線が必要かも確認します。

この例は入力を読んだあと1 byteずつ書き、書けなかった分を保持しません。連続データを中継する場合は
送信待ちのバッファを設け、`write()` の取得長だけ位置を進めます。切断時に未送信データを捨てるかも決めます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[USBSerial.ino](USBSerial.ino)
