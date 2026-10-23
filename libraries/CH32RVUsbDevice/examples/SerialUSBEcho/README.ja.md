# SerialUSBEcho

English: [README.md](README.md)

定義済みの `SerialUSB` だけで始める CDC ACM の最小例です。受信したバイトをそのまま返し、端末を開いている間は毎秒カウンタを送ります。

## 必要なものと配線

USB Device 対応ボード、データ通信できる USB ケーブル、PC のシリアル端末。追加 UART は使いません。

## 操作と期待する結果

1. 書き込み後、ボードの USB データ端子を PC に接続します。
2. 新しく現れた CDC ポートを、1200 bps 以外で開きます。DTR を有効にします。
3. `hello` を送ると同じバイト列が返ります。毎秒 `count 0`、`count 1`…も届きます。
4. 端末を閉じると周期送信は止まり、再度開くと再開します。

## 設定と制約

`SerialUSB.begin()` は既定の VID:PID `1209:0001` と製品名を使います。改行の加工はしないため、端末のローカルエコーを有効にすると二重に見えます。`SerialUSB` の真偽値は DTR の状態です。

1200 bps で開閉すると ISP への入口が作動します。X035/X033 以外の入口の制約は共通ガイドを参照してください。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `SerialUSB.begin()` | 定義済みの `USBDevice` と CDC ポートを起動します。引数の baud は UART のクロック設定ではありません。 |
| `available()` / `read()` | 受信済みバイト数を調べ、1 byteずつ取り出します。`read()` は未受信なら `-1` なので、戻り値はまず `int` で受けます。 |
| `operator bool` / `connected()` | ホストが DTR を有効にしたかを確認します。USB ケーブルが挿さっているかだけの判定ではありません。 |
| `write()` / `print()` / `println()` | それぞれバイト列、文字列化した値、改行付きの値を送ります。バイナリの中継は `write()` を使います。 |

この例はポーリングで受信するため、`deliverEvents()` は不要です。`onRx()` を登録する形に変える場合は
[SerialEvents](../SerialEvents/README.ja.md) のように `loop()` で通知を受け取ります。

エコーは行単位ではなくバイト単位です。端末のローカルエコーを無効にすると、ホスト自身の表示と返信を区別できます。
CDC の送信は相手が開いたまま読み出さないと待つことがあるため、周期ログは `if (SerialUSB)` の中に置きます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[SerialUSBEcho.ino](SerialUSBEcho.ino)
