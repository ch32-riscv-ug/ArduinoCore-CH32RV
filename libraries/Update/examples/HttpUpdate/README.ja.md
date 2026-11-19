# HttpUpdate

[English](README.md)

イーサネットのボードが、PC の HTTP サーバーから新しいスケッチを取ってきて、確かめてから入れ替えます（arduino-esp32 の
HTTPUpdate と同じ役割）。起動時に 1 回、`UPDATE_HOST` の `UPDATE_PATH` を GET し、長さは Content-Length から取ります。

## 必要なものと配線

- Ethernet の MAC を持つボード（V208、V307 / V317、V407 / V467、H415〜H417）。RJ45 を DHCP のある LAN に。同じ LAN に PC。
- シリアルモニタは `Serial`、115200 baud。

## 操作と期待する結果

1. 入れ替えるスケッチの `.bin`（Arduino の「コンパイルしたバイナリを出力」）を new.bin という名前で置いたフォルダで、
   `python3 -m http.server 8000` を動かします。
2. スケッチの `UPDATE_HOST` を PC の IP にして書き込みます。

```text
Address 192.168.1.81
image 3420 bytes, room for 210944
3420 bytes in 247 ms
OK, restarting into it
```

のあと、新しいスケッチで起動します。スケッチでないファイルなら `Wrong Magic Byte`、大きすぎれば `Not Enough Space` で、
このスケッチのままです。CH32V307 で 2026-11-20 に両方を確かめました。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `NetworkClient` | HTTP/1.0 で GET。ヘッダーを空行まで読み、Content-Length を取る。 |
| `Update.begin(length)` / `Update.capacity()` | 大きさを宣言。受け取れる最大は flash − このスケッチ。 |
| `Update.writeStream(client)` | 本体を流し込む。止まれば `setTimeout()` の時間で終わる。 |
| `Update.end()` / `CH32RV.restart()` | 確かめてから入れ替えて起動。 |

通信は暗号化も認証もしていません。外から届く場所で使うなら、HTTPS（NetworkClientSecure）とイメージの署名を考えてください。
入れ替え（先頭へのコピー）の間に電源が切れると、スケッチは書きかけになります（プローブで直せる）。
