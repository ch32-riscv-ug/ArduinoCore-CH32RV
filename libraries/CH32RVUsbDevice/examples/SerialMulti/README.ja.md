# SerialMulti

English: [README.md](README.md)

一つの USB デバイスに独立した二つの CDC ポートを作ります。Console と Data Link を分け、ログと機械向け通信を別の端末で確認できます。

## 必要なものと配線

PC のシリアル端末を二つと、必要なら UART モニタ。

## 操作と期待する結果

1. スケッチをビルドして書き込みます。
2. PC に現れる二つの CDC ポートをそれぞれ開きます。
3. Console に送った文字は `CONSOLE: ...`、Data Link では `DATALINK: ...` と返ります。
4. 各ポートに約3秒ごとに別の tick が届き、DTR の変更は UART に記録されます。

## 設定と制約

CDC は1つのデバイスに4ポートまで持てます。5つ目は `TooManyInstances` になります。CH32 ビルドでは ESP32-P4 向けの条件分岐は無効で、Telemetry は作られません。

CDC は通知用とデータ用の endpoint 番号を使い、二つなら計4番号です。固定 serial number を使う例なので、複数台を接続する場合は変更するか `nullptr` にしてチップ固有値を使ってください。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `Console(device, "Console")` / `DataLink(device, "Data Link")` | 同じデバイスへ別々の CDC を追加し、用途を名前で区別します。 |
| 各ポートの `available()` / `read()` / `connected()` | 受信データと DTR はポートごとに独立しています。一方を開いても、もう一方が開いたことにはなりません。 |
| `port()` | デバイス内での CDC の番号です。OS が割り当てる COM 番号や `/dev/ttyACM*` の数字とは別です。 |
| `device.begin(config)` | 各ポートの宣言と callback の登録後、デバイス全体を起動します。 |

二つの端末を開いて、それぞれ `CONSOLE:` / `DATALINK:` の返信を確認します。OS のポート名の並び順に
依存せず、interface 名と返信で識別してください。複数台を使う場合はデバイスの serial number も識別に使います。

機械向けプロトコルに変える場合、DataLink の周期 `data tick` と文字ごとのエコーを外します。
人向けログとコマンドを同じストリームへ混ぜると、相手のパーサーがログまで入力として扱ってしまいます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[SerialMulti.ino](SerialMulti.ino)
