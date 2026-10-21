# IP スタックの選定(検討中の提案)

2026-10-21 の調査にもとづく提案です。決定ではありません。ADR-0013 では、IP スタックは「今はまだ入れない」という扱いでした。TODO では「タスク周りの入れ替えが終わってから取りかかる」としています。

## 目的と、設計の下限にする系列

USB の NCM(デバイスとホスト)と、Ethernet の MAC を、1 つの IP スタックに載せます。その上に、Arduino の `Client` / `Server` / `UDP` と、EspUsbDevice の `ipConfig()` / `dhcpServer()` / `dhcpClient()` / `beginNetwork()` / `localIP()` を作ります。

| 系列 | Flash / RAM | リンク | 位置づけ |
|---|---|---|---|
| CH32X035 | 62 KB / 20 KB | USB の NCM だけ | 一番厳しい下限 |
| CH32V208 | 128 KB / 64 KB | 10M の MAC + PHY 内蔵 | オンボード NIC の下限 |
| CH32V307 / V317 | 256 KB / 64 KB | MAC 内蔵(10M の PHY 内蔵、100M / 1G は外付け) | |
| CH32V407 / V467 | 480 KB / 200 KB | MAC + PHY 内蔵 | |
| CH32H415〜H417 | 512 KB(SRAM で実行) | GMAC + 10/100 の PHY 内蔵(1G は外付け) | ベンチの LAN に使いたい |

- device-data の features.csv には、V208 の Ethernet が載っていません。device-data に追加を依頼します(TODO に記載)。

## 候補の比べ方と結果

比べたのは次の点です。
- ライセンス(閉じた製品のスケッチに静的にリンクしてよいか)
- 保守されているか
- OS なしで、協調実行(`loop()` / `delay()` から回す)で動くか
- 大きさ
- DHCP サーバー
- IPv6
- 複数の NIC、転送、NAT、WireGuard

| 候補 | ライセンス | 状態 | OS なし | DHCP サーバー | 複数 NIC / 転送 / NAT | WireGuard |
|---|---|---|---|---|---|---|
| **lwIP 2.2.x**(NO_SYS=1、raw API) | BSD-3(可) | 2.2.1(2025-02)、保守中 | 可 | 公式には無い(TinyUSB の dhserver(MIT)などを使う) | 複数 netif・転送は公式にある。NAPT は Espressif 版(esp-lwip)にだけある | wireguard-lwip / esp_wireguard(BSD-3) |
| Mongoose の内蔵スタック | GPLv2 か有償(**不可**) | 活発 | 可 | あり | インターフェース 1 つ。転送・NAT なし | なし |
| FNET | Apache-2.0(可) | 2023 年ごろから止まっている | 可 | あり | 複数 netif はある。NAT は見つからない | なし |
| uIP 1.0 | BSD | 2006 年で止まっている | 可 | なし | なし | なし |
| picoTCP | GPL(**不可**) | | 可 | あり | NAT あり | なし |
| CycloneTCP | GPLv2 か有償(**不可**) | 活発 | 可 | あり | 経路の表と NAT あり | なし |
| FreeRTOS-Plus-TCP、NetX Duo、Zephyr | | | RTOS が要る(**不可**) | | | |

ほかの Arduino コアは、ほとんどが lwIP を使っています。
- ESP32 は esp_netif の下に lwIP と FreeRTOS を置き、複数の NIC、経路の優先度、NAPT を持っています。
- RP2040(earlephilhower)は、lwIP を NO_SYS=1 で使っています。
  - 生のフレームのドライバを netif にする `LwipIntfDev<>` の上に、`WiFiClient` / `WiFiServer` / `WiFiUDP` を出しています。
  - **このコアに一番近い形です。**
- STM32duino と Teensy(QNEthernet)も lwIP です。QNEthernet は今は AGPL-3.0 です。

## 大きさ(rv32imac、-Os、gc-sections での試しのビルド)

| 構成 | Flash | RAM |
|---|---|---|
| lwIP を絞った版(IPv4・ARP・ICMP・UDP・TCP・DHCP クライアント、MSS 536、TCP 2 本) | 26.9 KB | 6.8 KB |
| 上に IPv4 の転送を足す | +0.3 KB | 0 |
| 上にフラグメント処理を足す | +2.7 KB | +0.6 KB |
| MSS 1460、ヒープ 8 KB、DNS 込み | 34.9 KB | 24.4 KB |
| IPv6 を足す | 56.6 KB | 27.3 KB |
| DHCP サーバー(TinyUSB の dhserver) | 1.4 KB | 0.5 KB |
| wireguard-lwip(ピア 1 つ) | 17.8 KB | 約 1.8 KB と x25519 用のスタック |

## もっと小さい候補との比較

同じ条件(rv32imac、-Os、gc-sections、newlib-nano)でビルドして、比べました。ドライバの代わりに、何もしない送受信の口をつないでいます。RAM には、送受信のフレームのバッファを含みます。

| 構成 | Flash | RAM | 中身 |
|---|---|---|---|
| lwIP(TCP と DHCP クライアント、小さい設定) | 32.1 KB | 7.4 KB | RAM の大半は、プール(MEM_SIZE 3 KB と、640 バイトの pbuf 4 つ) |
| lwIP(TCP なし。UDP・ICMP・ARP・DHCP クライアント) | 17.1 KB | 6.8 KB | |
| lwIP(TCP なし、DHCP なし) | 11.0 KB | 6.7 KB | |
| FNET(TCP / UDP のソケット、DHCP クライアントとサーバー) | 29.7 KB | 9.8 KB | 6 KB のヒープと、1.5 KB のフレームを含む |
| uIP 1.0(TCP・UDP・ARP・ICMP・dhcpc、接続 2 本) | 6.6 KB | 1.9 KB | バッファは 1514 バイトの 1 つだけ |
| 自前の最小版(ARP・IPv4・ICMP echo・UDP・DHCP クライアント) | 2.7 KB | 3.5 KB | 送信と受信のフレームで 3 KB |
| 自前の最小版 + TCP(接続 1 本、未確認の送信は 1 区切りだけ) | 4.1 KB | 4.0 KB | |

- 自前の最小版は、大きさを測るための試作です(scratch で書いたもの。動かしてはいません)。ARP の表は 4 つ、UDP の口は 4 つです。IP のフラグメントとオプションは扱いません。
- 自前の TCP と uIP の TCP は、未確認で送れるのが 1 区切り(536 バイト)だけです。
  - 相手が遅延 ACK(Linux は最短 40 ms)を使うと、1 往復ごとに待たされます。速さは、数十 KB/s 程度で頭打ちになります。
  - 順番の入れ替わった区切りは捨てて、再送を待ちます。
  - 窓のスケールも SACK もありません。
- uIP は、再送のときにアプリがデータを作り直す作りです。Arduino の `Client::write()`(書いたら終わり)とは合わないので、どのみち送信のバッファを足すことになります。2006 年で止まっていて、DHCP サーバーもありません。

**比べて分かったこと**
- lwIP は小さくできます。ただし、TCP を外しても 11〜17 KB は残ります。X035(62 KB)では、USB の NCM と合わせて約 50 KB の見込みです。入るものの、余裕はありません。
- 自前の最小版は、lwIP の 1/4〜1/8 です。X035 でも十分入ります。代わりに、次のものを自分で持つことになります。
  - TCP の正しさ。再送、窓、異常なパケットへの耐性、セキュリティの修正。
  - 速さ。上に書いた頭打ちがあります。
  - 段階 (b)(c)。複数の NIC、転送、NAPT、WireGuard は、どれも書き起こしです。WireGuard の既存の移植(wireguard-lwip / esp_wireguard)は、lwIP が前提です。
- FNET は、lwIP と大きさが変わらないうえに、止まっていて、NAT も WireGuard もありません。lwIP より先に選ぶ理由は見つかりませんでした。

**組み合わせの案**
- 案 1: **全部 lwIP。** X035 では TCP を外した構成にするか、TCP 込みで 50 KB 前後を受け入れる。保守は 1 つで済みます。
- 案 2: **lwIP と、X035 向けの自前の最小版。** 同じ Arduino の API(`UDP` と、接続 1 本の `Client` / `Server`)の下に、2 つのスタックを置きます。X035 では軽くなります。代わりに、試験と保守が 2 倍になります。
- 案 3: **自前だけ。** 段階 (a) を超える機能はすべて書き起こしです。お勧めしません。

いまのお勧めは **案 1** です。X035 で USB の NCM と lwIP を実際に組み合わせて測ります。入らない、または RAM が足りないと分かったときに、案 2 に切り替えます。

## 提案

**lwIP 2.2.x を NO_SYS=1 と raw API で使い、その上に自前の薄い netif の層と Arduino の API を載せます。**

**構成**
- ドライバ(USB の NCM のデバイスとホスト、各系列の Ethernet の MAC)は、`linkoutput` と受信の入口だけを持ちます。受信は、PBUF_REF を使えば DMA のバッファからコピーせずに渡せます。
- スタックのタイマー(`sys_check_timeouts()`)は、`ch32rv_usb_task()` と同じ協調実行の入口から回します。コールバックは、割り込みではなく通常の文脈で呼ばれます。
- API は、Arduino の `Client` / `Server` / `UDP` に合わせます(arduino-pico の ClientContext が手本です)。
- DHCP サーバーは、TinyUSB の dhserver を元にするか、自分で書きます。

**ビルド時の設定への対処**
- lwIP の設定(プールの大きさ、MSS、IPv6 など)は `lwipopts.h` で決まるビルド時のものです。このコアの「実行時に設定する」方針とは合いません。
- そこで、系列ごとに固定の構成を 2〜3 種類用意します(X035 向けの小さい版、V20x / V30x 向け、IPv6 付きの H417 / V407 向け)。
- どの NIC を使うか、アドレス、DHCP の動き方は、実行時に選べるようにします。
- Arduino IDE は、スケッチの define を使わずにライブラリをビルドするので、スケッチごとの調整は、どのみち効きません。

## 段階と、それぞれの最低ライン

| 段階 | 内容 | 最低ライン | 備考 |
|---|---|---|---|
| (a) | NIC 1 つで UDP / TCP、DHCP のクライアント / サーバー | **X035**(ぎりぎり)、V208 なら余裕 | X035 は USB の NCM と合わせて 45〜50 KB / 62 KB の見込み。約束する前に、実際に組み合わせて測る |
| (b) | 複数の NIC、既定のゲートウェイ / 経路の優先度、IPv4 の転送、NAPT | **V208** | 転送は +0.3 KB。NAPT は esp-lwip の `ip4_napt.c` を 2.2.x に移す必要があり、その保守はこちらで持つ(コード 3〜5 KB、RAM は表の項目数で決まる)。X035 はリンクが 1 本なので意味がない |
| (c) | WireGuard | **V208 / V307**(RAM 64 KB)、V407 / H417 は余裕 | Flash 約 18 KB。X25519 は 144 MHz で 1 回あたり数十 ms の見込み(**未実測**)で、ハンドシェイクで 4〜5 回使う。協調実行を止めないよう、処理を分ける必要がある。本物の乱数源と時計が要る。X035 は無理 |

## H417 の LAN(ベンチの通信経路の候補)

- 内蔵の PHY は 10/100BASE-TX で、評価ボードの RJ45(HR911105A)に直結されています。
- レジスタを扱う部分は、EVT の MAC_RAW の例に公開されています。閉じているのは、EVT の TCP/IP スタック(libwchnet.a)だけです。
- 生のフレームのドライバは 2〜3 KB 程度の見込みです。
- 確かめること:
  - ETH の DMA が共有 SRAM(0x20100000)に届くか(EVT は V5F の DTCM で動かしている)。
  - PHY のクロックは HSE(25 MHz)から作るので、HSE を立ち上げる必要がある。
  - EVT が書いている、資料にない PHY の設定(0x4002A00C)と、極性 / ESD の回避の処理が要る。
- ベンチで使うなら、段階 (a) の上に、UDP でのログとコマンド、DFU 相当の書き込み(TFTP など)を載せる形になります。USB にも SWD にも頼らない回復の経路になります。

## 懸念

- ビルド時の設定(上の対処)。
- NAPT は公式の lwIP の外にあり、移植と保守を持つことになる。
- DHCP サーバーの元(dhserver)は、デモ程度の出来。
- lwIP にも CVE がある(SNMPv3 など)。アプリ層(SNMP など)は無効にしておく。
- 予備の候補は、X035 向けの自前の最小版(上の「もっと小さい候補との比較」の案 2)。FNET は、lwIP より先に選ぶ理由が見つからなかった。

## 決めること

1. lwIP を採るか。X035 向けに自前の最小版を併せて持つか(案 1 か案 2 か)。
2. 取り込み方。以前の TinyUSB のように vendor/ に lock を置いて取り込むか(vendor-policy.ja.md)。
3. タスク周りの入れ替えとの順番。TODO では入れ替えの後としています。ベンチの LAN のために先に始めるか。
4. 系列ごとの固定の構成をどう分けるか。
5. ベンチの PC に、H417 の RJ45 につなげる空きの LAN の口があるか。
