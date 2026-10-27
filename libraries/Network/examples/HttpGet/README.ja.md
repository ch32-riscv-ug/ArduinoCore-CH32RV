# HttpGet

[English](README.md)

ホスト名で、30 秒ごとに HTTP のページを取得します。状態コード、本文の大きさ、かかった時間を表示します。失敗も種類ごとに分けて表示します。名前が引けない、接続を断られた、期限までに答えが無い、の 3 つです。

## 操作

既定の `example.com` なら、`example.com (アドレス): HTTP 200, N bytes in T ms` と出ます。インターネットにつながない環境では、`HOST` を手元の PC にし、その PC で `python3 -m http.server 80` を動かして試せます。存在しない名前（`DNS: cannot resolve`）と、何も待ち受けていないポート（`Connect to ... failed`）も試してください。

## API と改造時の注意

先に `Network.hostByName()` で名前を引きます（数字のアドレスもそのまま使えます）。DNS の失敗を、接続の拒否と取り違えないためです。リクエストは HTTP/1.0 です。本文はそのまま（chunked にならずに）届き、終わるとサーバーが接続を閉じます。サーバーが閉じなくても、`TIMEOUT_MS` で読むのをやめます。HTTPS にするときは、`NetworkClient` の代わりに `NetworkClientSecure` を使い、サーバーの CA 証明書を渡します。

## 実行前の準備

Ethernet の MAC を持つボードを選び（生成された profile は CH32V307。V208、V317、V407 / V467、H415〜H417 も同じ書き方で動きます）、RJ45 を DHCP サーバーのあるネットワークにつなぎます。ログはハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。基板の USB 端子にはこの UART は出ていません。

通信の処理は `delay()` の中と各 `loop()` の後で進みます。スケッチが poll の関数を呼ぶ必要はありません。ビルドが通っても、ケーブルやネットワークの確認にはなりません。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
