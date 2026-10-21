# IP スタック(方針と作業計画)

2026-10-21 にユーザーと決めた方針と、作業の計画です。ADR-0013 で「今はまだ入れない」としていた IP スタックに、この方針で取りかかります。実装して動いたところで ADR にします。

候補の比較(lwIP、uIP、FNET、自前の最小版の大きさ)は、コミット becf0f1 の版にあります。

## 範囲

| 系列 | IP スタック | リンク |
|---|---|---|
| CH32V208 | 対象(設計の下限。128 KB / 64 KB) | 10M の MAC + PHY 内蔵 |
| CH32V307 / V317 | 対象 | MAC 内蔵(10M の PHY 内蔵、100M / 1G は PHY 外付け) |
| CH32V407 / V467 | 対象 | MAC + PHY 内蔵 |
| CH32H415〜H417 | 対象 | GMAC + 10/100 の PHY 内蔵 |
| CH32X035 ほか、MAC の無い系列 | **対象外** | USB の NCM の生フレーム(`CH32RVUsbDeviceNet`)まで |

- 対象は、Ethernet の MAC を持つ系列です。同じ系列では、USB の NCM(PC からの設定画面など)も同じスタックに載せます。
- X035 は対象外です。X035 に USB の NIC を足してまでネットワークにつなぐなら、ESP32 を使うほうが合います。X035 でも入る見積もりでしたが(デバイスの NCM と IP スタックで Flash 約 45 KB / 62 KB)、アプリの残りが小さく、割に合いません。

## 目標の段

| 段 | 内容 | 扱い |
|---|---|---|
| 2 | NIC 1 つで IPv4。固定アドレス、DHCP のクライアントとサーバー、UDP、TCP のクライアントとサーバー、DNS | 作る |
| 3 | mDNS、時刻合わせ(SNTP)、HTTP サーバー、MQTT / HTTP のクライアント | 作る(ライブラリや例として) |
| 4 | IPv6 | 段 2 の後で、大きさを見て決める |
| 5 | TLS | できるかを確かめる(センサーノードからクラウドへ直接つなぐのに要る) |
| 6〜8 | 複数の NIC と経路の優先度、転送と NAPT、WireGuard | できるかを確かめるだけ。だめなら諦める |

段 1(リンクの生フレーム)は、USB の NCM ですでにあります。

## やり方

**まず lwIP で一式を作り、測ってから詰めます。** 対象を MAC のある系列に絞ったので、lwIP の大きさ(段 2 で Flash 約 30 KB、RAM 約 7〜20 KB。設定による)は問題になりません。

- **lwIP 2.2.x を NO_SYS=1(OS なし)、raw API で使います。** 協調実行です。`loop()` の外(`yield()` / `delay()` の中)からタイマーと受信を回し、コールバックは割り込みの外で呼びます。
- **取り込み:** `libraries/Network/src/lwip/` に、上流のリリースから必要なファイルだけを固定の写しとして置きます。版と各ファイルのハッシュは `vendor/lwip.lock.toml` に記録し、`tools/vendor/vendor_lwip.py` で取り込みと照合をします(vendor-policy.ja.md)。
- **設定(`lwipopts.h`):** lwIP の設定はビルド時に決まります。スケッチの define は使わず、系列のマクロ(`CH32RV_SERIES_*`)で、RAM の量に合わせた固定の構成を選びます。
- **API は ESP32 の Arduino コア 3.x の Network ライブラリに合わせます。** `Network`、`NetworkInterface`、`NetworkClient`、`NetworkServer`、`NetworkUDP` です。ESP32 のスケッチがそのまま、またはわずかな修正で動くようにします。EspUsbDevice の `ipConfig()` / `dhcpServer()` / `dhcpClient()` / `beginNetwork()` / `localIP()` も、この上に作ります。
  - ESP32 の実装(LGPL のものを含む)のコードは写しません。名前と動きだけを合わせます。
- **NIC のドライバとの境目:** ドライバは「フレームを 1 つ送る」「受けたフレームを渡す」「リンクの上下を知らせる」だけを持ちます。Ethernet の MAC、USB の NCM(デバイス)、試験用の PC の tap は、どれも同じ口でつながります。
- **試験:** 機材が無くても進められるように、ライブラリを PC でもビルドします。Linux のユーザー名前空間の tap を相手に、DHCP、DNS、TCP、UDP、パケットの落ちと入れ替わりを試します(root は要りません)。

## lwIP の raw API で気をつけること

2026-10-21 の試しの実装で確かめたことです。

- `tcp_write()` や `tcp_close()` は、メモリが足りないと `ERR_MEM` で失敗します。送信中のデータがないコネクションには `sent` のコールバックが来ないので、そこで再試行を待つと止まります。`tcp_poll()` のコールバックで再試行します。
- 受けた pbuf をアプリが持ったままにすると、受信用のプールが尽き、相手からの ACK も受けられなくなって止まります。Client の層は、受けたデータを自分のバッファに写して、pbuf をすぐに返します。

## 作業の順番

1. lwIP の取り込み(ロック、取り込みの道具、照合の試験)。
2. Network ライブラリの土台: lwIP を回す仕組み、`NetworkInterface`(固定アドレス、DHCP のクライアント)、ドライバとの境目。
3. `NetworkUDP`、`NetworkClient`、`NetworkServer`、`Network.hostByName()`。
4. DHCP サーバー。
5. PC の tap での試験を、テストの仕組み(tests/)に入れる。
6. 系列ごとの `lwipopts.h` と、クロスビルドでの大きさの確認。
7. USB の NCM(デバイス)のつなぎ込み。
8. Ethernet の MAC のドライバ(機材が要る。V208 / V307 / V407 / H417)。
9. 段 3 以降。

## 決めていないこと

- 段 4 以降の採否(段 2、3 を測ってから)。
- USB の NCM のつなぎ方。EspUsbDevice は NCM のクラスに `beginNetwork()` を持たせています。`CH32RVUsbDevice` が Network ライブラリに依存しないようにするか(つなぎの小さなクラスを Network 側に置くなど)は、手順 7 で決めます。
