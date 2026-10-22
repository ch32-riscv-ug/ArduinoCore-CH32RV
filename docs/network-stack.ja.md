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

1. lwIP の取り込み(ロック、取り込みの道具、照合の試験)。**済み**
2. Network ライブラリの土台: lwIP を回す仕組み、`NetworkInterface`(固定アドレス、DHCP のクライアント)、ドライバとの境目。**済み**
3. `NetworkUDP`、`NetworkClient`、`NetworkServer`、`Network.hostByName()`。**済み**
4. DHCP サーバー。**済み**(自前。TinyUSB の dhserver は使わない)
5. PC の tap での試験を、テストの仕組み(tests/)に入れる。**済み**(`tests/unit/network`)
6. 系列ごとの `lwipopts.h` と、クロスビルドでの大きさの確認。**済み**(V208 でネットワークの分が Flash 約 44 KB、静的な RAM 約 22 KB)
7. USB の NCM(デバイス)のつなぎ込み。**済み**(案 B: `CH32RVUsbDeviceNet` に EspUsbDevice と同じ呼び出し、中身は Network ライブラリの `NetworkUsbNcm`。PC と X035 の生フレームの試験まで。対象の系列の USB デバイスのドライバ待ち)
8. Ethernet の MAC のドライバ(機材が要る。V208 / V307 / V407 / H417)。**H417 は済み**(2026-10-22、下の「Ethernet(H417)」)。V407 / V467、V307、V317、V208 / V203RB はビルドまで(実機は未。下の「ほかの系列」)。
9. 段 3 以降。mDNS と SNTP は**済み**(lwIP の apps。PC で試験)。
   - HTTP サーバー、MQTT / HTTP のクライアントは、**自前では作らず、Arduino で広く使われている既存のライブラリを使ってもらう**ことにしました(2026-10-21)。
   - 次のライブラリが、手を入れずに `NetworkClient` / `NetworkClientSecure` / `NetworkServer` の上で動くことを、PC 上で確かめました。

     | ライブラリ | 役目 | 確かめたこと |
     |---|---|---|
     | PubSubClient 2.8(MIT) | MQTT | 購読と配信の往復。平文と TLS |
     | ArduinoMqttClient 0.1.8(LGPL-2.1) | MQTT | 同上 |
     | ArduinoHttpClient 0.6.2(Apache-2.0) | HTTP クライアント | GET。HTTP と HTTPS |
     | aWOT 3.5.0(MIT) | HTTP サーバー | GET と POST |

     - 相手は、Python で書いた最小限の MQTT のブローカーと、HTTP(S) のサーバーです。
     - 試験は作業用の場所で行い、リポジトリには入れていません。外部のライブラリに依存させないためです。
   - TLS でつなぐときは、IP アドレスではなくホスト名で接続します。証明書の名前と照らし合わせるためで、ESP32 と同じです。
   - この確認の中で、Network ライブラリの不具合を 1 つ見つけて直しました。`NetworkClient::read()` と `peek()` が、データが無いときにスタックを回していなかったため、`Stream` の待ち付きの読み出し(`readStringUntil()` など。aWOT が使う)が、時間切れまで受信できずにいました。

## Ethernet(H417)

`ETH`(`NetworkEthernet.h`、`#include <Network.h>` で入る)。ESP32 の `ETH` と同じく `ETH.begin()` で DHCP を始めます。ヘッダーの名前は ESP32 の `ETH.h` にはしていません。

- **中身:** 内蔵の MAC と 10/100 の PHY。割り込みは使わず、`poll()`(ネットワークの処理の中)で受信の記述子を見て、20 ms ごとに PHY を見ます。受信 8 本、送信 4 本の記述子(1536 B)で、RAM は約 18 KB です。
- **クロック:** HSE(25 MHz)→ ETH の PLL(RCC CTLR の bit 26)→ MAC のクロック。コアの `SystemInit()` は、デバッガがクロックを動かしたあとに走り直し、HSE と ETH の PLL を切ります(CTLR を `&= 0x6aa6ffff`)。`poll()` は、それを見つけると入れ直します(`clockRestarts()` で数が分かる)。DMSeq のコンソールで試験をすると、毎回 1 回起きます。
- **PHY:** EVT(`eth_driver_100M.c`)の回避策を 3 つ入れています。リンクが上がらないときの P/N の極性の切り替え、100M のリンクで流れていないときのリセット、ESD のあとのリセットです。リンクが上がったときの PHY の調整の書き込み(0x4002A00C)も EVT のとおりです。
- **チェックサム:** IPv4、TCP、UDP、ICMP のチェックサムは MAC が付け、確かめます(`LWIP_CHECKSUM_CTRL_PER_NETIF` でこのインターフェースだけ lwIP の計算を止める)。受信は 17.6 → 19.9 Mbit/s、送信は変わらず。
- **MAC アドレス:** チップの固有 ID から作るローカルなアドレス(先頭 0x06)。USB NCM のものとは別。`setMacAddress()` で変えられます。EVT は ESIG の 6 バイトをそのまま使いますが、それはグローバルな MAC の形になり、他社の OUI と重なりえます。
- **アドレスの絞り込み:** 自分の MAC、ブロードキャスト、全マルチキャスト(mDNS のため)。
- **対象:** H417(ETH は QEU6、MEU6、WEU6 にある)。ほかの系列は下の「ほかの系列」。

### ほかの系列(2026-10-22、ビルドまで。実機では試していない)

各系列の EVT の NetLib から移しました。H417 と同じ Synopsys の MAC の系列は `NetworkEthernet.cpp`、別の MAC の V208 / V203RB は `NetworkEthernet10m.cpp` です。

| 系列 | PHY | クロック | 回避策(EVT のとおり) | 注意 |
|---|---|---|---|---|
| V407 / V467 | 内蔵 10/100(H417 と同じ) | HSE 25 MHz → ETH の PLL(CTLR の bit 22/23)、MAC のクロックは 0x40021014 の bit 14 | なし(V407 の EVT にない) | コアの HCLK は 20 MHz。MDC は /16。100M で DMA が間に合うかは未確認 |
| V307 | 内蔵 10BASE-T | HSE → PLL3 をちょうど 60 MHz(PREDIV2 と PLL3MUL を HSE から計算)、EXTEN の bit 2 | T/R と P/N の切り替え、5 秒でリセット、リンクが落ちたら PHY のクロックを 500 ms 止める、リンクのあと CRC エラーで極性を反転 | チップのリビジョン 0x20 の MAC の作り直しは移していない。RAM が 64 KB で HTTPS のヒープが入らない(試験は記録して飛ばす) |
| V317 | 内蔵 10/100(内部の RMII) | HSE → PLL3 を 100 MHz、RMII の端子(PD8、PE8〜PE15)と AFIO の RMII 選択 | P/N(0x10 の bit 12、0x16 の bit 5)、リンクを早める手順、100M のリンクの確認 | RAM は V307 と同じ |
| V208 / V203RB | 内蔵 10BASE-T(別の MAC) | HCLK を 1/1 か 1/2 で 60 MHz(CFGR0 の ETHPRE) | T/R と P/N の切り替え、リンクが落ちたら 500 ms、CRC エラーで極性を反転 | **HCLK が 60 / 120 MHz のときだけ動く。コアは 96 MHz なので `begin()` は false を返す**(クロックの選択肢が要る)。受信は割り込みで 4 つのバッファーを回す。チェックサムの機能も、アドレスの絞り込みもない(全部受けて lwIP が捨てる)。TLS は乱数の回路がないので無い |

- HSE の周波数は `ETH.setHseFrequency()` で変えられます(V307 / V317。既定 8 MHz)。60 / 100 MHz がちょうど作れない値なら、`begin()` は false を返します。
- 試験(`--case eth`)は、どの系列でもそのまま使えます。V208 / V203RB では HTTPS の段を飛ばし、V307 / V317 で HTTPS のヒープが足りないときは、記録して飛ばします。

### 測った値(2026-10-22、評価ボード、100M 全二重、LAN の PC と)

| 項目 | 値 |
|---|---|
| ping(20〜300 回) | 平均 0.30〜0.46 ms |
| TCP の 64 B のエコー(1000 回) | 中央値 0.26 ms、99% 1.0 ms |
| TCP で受ける / 送る | 19.6〜19.9 / 23.7〜27.1 Mbit/s |
| HTTP の GET 100 B | 1 回 1.5 ms |
| HTTP の GET 1 MB / 200 KB | 2.4 MB/s / 246 ms |
| HTTPS のハンドシェイク(TLS 1.3) | ECDSA P-256 で 1.72〜1.80 s、RSA-2048 で 1.25〜1.29 s、検証なしで 1.18 s |
| HTTPS の 1 MB | 137〜154 kB/s |
| HTTPS で増えるヒープ | 約 33 KB(外のサイトで 37〜40 KB) |
| 外のサイト(DNS を通して) | example.com、www.google.com につながった(google の 93 KB を 283 kB/s) |

- V3F の 100 MHz での値です。HTTPS の速さは mbedTLS の計算で決まっています。
- **ベンチの注意:** ベンチの PC は、有線と Wi-Fi の両方で同じサブネットにつながっていて、どちらでも ARP に答えます(`arp_ignore=0`)。ボードの ARP の表で PC が Wi-Fi 側の MAC になると、ボードから PC へのフレームは Wi-Fi のアクセスポイントでビーコン(102.4 ms)ごとにためられ、ping が 100 ms〜900 ms 遅れました。ボードのドライバの問題ではありません。試験は、PC の有線側の MAC を ARP に固定します(`etharp_add_static_entry`。そのために `ETHARP_SUPPORT_STATIC_ENTRIES` を 1 にした)。PC 側で `arp_ignore=1`、`arp_announce=2` にすれば要らなくなります。

### 試験

`tests/run_hardware.py --case eth`(`tests/instrumented/eth/test_eth_contract.py`、スケッチは `tests/sketch_support/eth/eth.ino`)。DHCP、ping、UDP と TCP のエコー、両方向 4 MB の中身、HTTP の GET の CRC、HTTPS(試験ごとに作る CA で ECDSA と RSA を検証、検証なし、別の CA では断られること)を確かめます。速さは junit の property に残し、判定は中身と 5 Mbit/s の下限だけです。

## TLS(段 5): mbedTLS と BearSSL の比較(2026-10-21 の測定)

TLS は入れると決めました。下の比較をもとに、**Mbed TLS 4.1 を使うと決めました**(2026-10-21)。保守と TLS 1.3 に進めることを、大きさと速さより重く見ました。実装は `libraries/NetworkClientSecure`(ESP32 と同じ API)です。候補は、ライセンスで使える mbedTLS(Apache-2.0)と BearSSL(MIT)です。

**比べた構成:** TLS 1.2 のクライアント。両方で同じ機能にそろえました。
- 鍵交換と証明書: ECDHE、ECDSA と RSA の証明書。
- 暗号: AES-128/256-GCM、ChaCha20-Poly1305。
- 曲線とハッシュ: P-256 / P-384 / X25519、SHA-256 / 384。
- そのほか: SNI、X.509 の検証。
- 受信のバッファは 16 KB です。小さくする拡張(MFL)に応じるサーバーは少ないためです。

測るための道具と設定は、作業用のディレクトリに置いたままです(リポジトリには入れていません)。

### 大きさ(rv32imac、-Os、gc-sections)

| | Flash | 静的な RAM | ハンドシェイク中のヒープのピーク |
|---|---|---|---|
| mbedTLS 3.6.7 | 89.6 KB | 1.3 KB | 25.5〜37.1 KB |
| mbedTLS 3.6.7(速い設定: 窓 4、固定点の最適化) | 95.6 KB | 1.3 KB | +約 2 KB |
| mbedTLS 4.1.1 | 100.6 KB | 2.9 KB | 28.6〜38.7 KB |
| mbedTLS 4.1.1(速い設定) | 106.5 KB | 2.9 KB | +約 2.5 KB |
| BearSSL 0.6(半二重。バッファ 1 つ) | 68.2 KB | 25.3 KB | 使わない |
| BearSSL 0.6(全二重) | 68.2 KB | 41.7 KB | 使わない |

- ヒープは、PC 上で openssl を相手に測りました(64 ビットなので、実機より少し大きく出ます)。最大は、根 RSA-4096 → 中間 RSA-2048 → サーバー RSA-2048 の鎖のときです。
- スタックの最大の深さは、どれも 9〜10.5 KB でした(PC での値)。
- TLS 1.3 を足すと、mbedTLS 3.6.7 は 126 KB になりました。BearSSL は TLS 1.3 に対応していません。
- 採用した 4.1.1 では、1.3 を足した分は Flash 約 15 KB でした。ライブラリには 1.3 を入れています(既定は 1.2 と 1.3 の両方を名乗り、サーバーが選ぶ。`setTlsVersions()` で絞れる)。
  - 測る中で、RSA の証明書のサーバーと 1.3 でつながらない問題が見つかりました。原因は `MBEDTLS_X509_RSASSA_PSS_SUPPORT` の抜けで、これが無いとクライアントが RSA-PSS の署名に対応していると名乗りません。足して直しました(RSA-PSS で署名された証明書も、これで受け入れられます)。
- mbedTLS 4.1 で使える P-256 の高速な実装(p256-m)は、P-256 以外の曲線をすべて無効にする必要があります。P-384 と X25519 が要るので、使えません。

### つながるか(PC 上で openssl 3.5 のサーバー、4 種類の証明書)

- 1 段の ECDSA P-256 と RSA-2048、3 段の鎖(Let's Encrypt に似せた ECDSA の鎖と RSA の鎖)の 4 種類で試しました。どちらも全部つながり、検証できました。
- 違いが出たのは、鎖の中に同じ証明書が重複して送られた場合です(サーバー側の設定の誤り)。mbedTLS は受け入れ、BearSSL は拒みました(発行者の名前が合わない、というエラー)。実際のサーバーにも、こうした誤りはときどきあります。

### 速さ(X035 の 48 MHz で実測。V208 / V307 の 144 MHz では約 1/3 と見込む)

| 処理 | mbedTLS 既定 | mbedTLS 速い設定 | BearSSL |
|---|---|---|---|
| X25519(生成 + 共有) | 1.83 s | 1.83 s | 0.43 s |
| P-256 の ECDHE(生成 + 共有) | 2.8 s | 1.7 s | 1.12 s |
| ECDSA P-256 の検証 | 3.04 s | 1.94 s | 1.20 s |
| ECDSA P-384 の検証 | 6.91 s | 4.25 s | 6.75 s |
| RSA-2048 の検証 | 0.35 s | 0.35 s | 0.38 s |
| SHA-256 | 141 KB/s | 141 KB/s | 195 KB/s |
| AES-128-GCM | 29 KB/s | 29 KB/s | 48 KB/s |
| ChaCha20-Poly1305 | 未測定 | 未測定 | 168 KB/s |

- mbedTLS の速さは、3.6.7 で測りました。4.1 の楕円曲線の計算は、同じコード(`drivers/builtin`)なので、同じと見ています(4.1 自体は測っていません)。

**ハンドシェイクの公開鍵の計算の合計**(上の表から計算。144 MHz の見込み)
- **ECDSA の鎖**(P-384 の検証 2 回、P-256 の検証 1 回、X25519): mbedTLS 既定 6.2 s、mbedTLS 速い設定 4.1 s、BearSSL 5.0 s。
- **RSA の鎖**(RSA-4096 の検証 1 回、RSA-2048 の検証 2 回、X25519): mbedTLS 約 1.3 s、BearSSL 約 0.9 s。RSA-4096 は測っておらず、RSA-2048 の 4 倍と見積もりました。

### 証明書バンドル(ESP32 の setCACertBundle に当たるもの)

- ESP32 では、ESP-IDF の `esp_crt_bundle` が mbedTLS の検証のコールバックに入り込んで実現しています。バンドルは、根の証明書の名前(DN)と公開鍵だけを、名前の順に並べたものです(Mozilla の 144 個で約 56 KB)。
- **BearSSL:** 信頼の起点(`br_x509_trust_anchor`)が、もともと名前と公開鍵の配列です。Flash に置いた配列を、そのまま渡せます。
- **mbedTLS:** 信頼する CA をその場で渡すコールバック(`mbedtls_ssl_conf_ca_cb()`)は、解析済みの証明書を返す形です。証明書を丸ごと持つと、約 200 KB になります。名前と公開鍵だけで済ませるには、ESP-IDF と同じく、検証のコールバックで署名を自分で確かめる処理を書くことになります。

### 保守

- **mbedTLS:** Arm / TrustedFirmware のプロジェクトで、長期サポートの版があります。
  - 3.6 のサポートは 2027 年 3 月まで、4.1 は 2029 年 3 月までです。採るなら 4.1 です。
  - セキュリティの修正は、年に数回のリリースで出ています。
  - TLS 1.3 に対応しています。
- **BearSSL:**
  - 開発者は 1 人です。正式リリースは 0.6(2018 年)のままです。
  - git の本流には、2019 年以降 30 件のコミットがあり、2026 年 4 月にも受信の処理の不具合が直っています(GCM / ChaCha20 には関係しない部分)。採るなら、本流のコミットを固定して使うことになります。
  - TLS 1.3 には対応していません。

## 決めていないこと## 決めていないこと

- 段 4 以降の採否(段 2、3 を測ってから)。
- V307 で TLS を使うときの RAM。静的な分が約 30 KB、ハンドシェイク中のヒープが 30〜40 KB で、64 KB に収まらないおそれがある。V307 はオプションバイト(`RAM_CODE_MOD`)で Flash と RAM の分け方を変えられるが、コアはまだ対応していない(メニューと書き込みの道具の対応が要る)。
