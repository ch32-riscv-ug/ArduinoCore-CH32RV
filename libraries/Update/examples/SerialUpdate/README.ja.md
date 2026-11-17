# SerialUpdate

English: [README.md](README.md)

UART（`Serial`）で長さとイメージを受け取り、確かめてから入れ替えます。

## 必要なものと配線

板と、`Serial` の UART につながる USB シリアル変換。PC に Python と pyserial。

## 操作と期待する結果

1. 書き込むと `Update: up to <バイト数> bytes` と出ます。これが受け取れる最大です。
2. 入れ替えるスケッチの `.bin` を、スケッチの冒頭のコメントにある Python の 1 行で送ります（長さの行、続けてバイト列）。
3. `OK, restarting into it` の後、新しいスケッチで起動します。
4. 途中で止まると `Stream Read Timeout`、スケッチでないものは `Wrong Magic Byte` で、このスケッチのままです。

## 設定と制約

- 受け取れるのは「flash − このスケッチ」まで。V003（16 KB）なら約 10 KB のスケッチまでです。
- 入れ替え（先頭へのコピー）の間に電源が切れると、スケッチは書きかけになります。プローブで直せます。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `Update.begin(size)` | 大きさを宣言します。staging に入らなければ `Not Enough Space`。 |
| `Update.writeStream(Serial)` | `size` バイトそろうまで読みます。`Serial.setTimeout()` の間何も来なければ失敗です。 |
| `Update.end()` | 確かめて、再起動で入れ替わる状態にします。 |
| `CH32RV.restart()` | 入れ替えて起動します。 |

## 関連資料

- [Update ライブラリ](../../README.ja.md)

[SerialUpdate.ino](SerialUpdate.ino)
