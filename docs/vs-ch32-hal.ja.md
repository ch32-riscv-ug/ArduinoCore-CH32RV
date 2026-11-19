# ch32-rs（Rust の HAL と BLE）との実装の範囲の比較（2026-11-20）

ch32-rs の Rust の実装と、このコアの範囲を比べたメモです。こちらに無いもの、向こうの知見で使えるものを拾うのが目的です。
見たのは各リポジトリの README・Cargo.toml・src と examples の一覧です。個々のドライバのコードは読んでいません。

| リポジトリ | 見た版 | 中身 |
| --- | --- | --- |
| [ch32-rs/ch32-hal](https://github.com/ch32-rs/ch32-hal) | f729d44（2026-11-18） | Embassy の HAL（async、DMA）。MIT / Apache-2.0。開発中 |
| [ch32-rs/ch32-data](https://github.com/ch32-rs/ch32-data) | 同じ日の main | レジスタと石の能力（crate `ch32-metapac`）。ch32-hal の元 |
| [ch32-rs/wchble](https://github.com/ch32-rs/wchble) | 97ebbc3（2026-11-19） | BLE の無線と LL（約 3 万行の Rust）。Apache-2.0 |

## 石

| | ch32-hal | このコア |
| --- | --- | --- |
| 両方 | V002〜V007・M007、V003、V103、V203、V208、V303 / V305 / V307、V407 / V467、X033 / X035、L103、H417（V3F・V5F の両方） | 同じ |
| こちらだけ | — | M030、M103、V205、V317、H415 / H416、X305 / X315、UIAPduino |
| 向こうだけ | CH641、CH643（V2A・V4C の USB PD や LED の石）。CH645・CH564 は募集中 | 無し（device-data に無い） |

ch32-data の BLE.csv には CH57x / CH58x / CH59x（BLE の単体の石）と V208 / F208 が並びます。CH5xx はこのコアの範囲の外です。

## 周辺

ch32-hal の README の表（✅ = 動く見込み、❓ = 未試験）と src を、こちらの [unimplemented.ja.md](unimplemented.ja.md) と比べました。

**向こうにあって、こちらに無いもの**
- **I3C**（V4x7・H41x）: こちらは未実装（優先 2）。ch32-hal は src/i3c（DMA つき）と例 hal_i3c（V467）を持っています。
  作るときの比べ先になります。
- **CH641・CH643**: 石そのものがこちらにありません。足すには device-data に入れることから始まります。
- **async（Embassy）の形**: 全ドライバが async/await です。こちらは Arduino の同期の API に、`transferAsync()` や
  コールバックを足す形です。考え方が違うので、追いかける対象にはしません。
- **I2C スレーブの DMA**: こちらは割込みで足りるとして作っていません（ユーザーの判断で任意。2026-11-17）。

**こちらにあって、向こうに無いもの（主なもの）**
- USB ホスト（FS / HS / split / MSC）、USB デバイスの各クラス（CDC・HID・MSC・MIDI・NCM・UAC・UVC・DFU など）
- ADC のストリーム・注入・デュアル、HSADC、OPA、コンパレータ、DAC の波形（向こうの DAC は H4 だけ）
- RTC のカレンダーとアラーム、lightSleep / deepSleep、PVD、EXTI の線
- ECDC（AES / SM4）、GPHA、UHSIF、SD（SPI と SDIO）、ファイルシステム、ネットワーク（TCP/IP・TLS・DHCP サーバー）
- H41x の V3F を同じスケッチの 2 つ目のコアとして使うこと（`setup1()` / `loop1()`、IPC・HSEM）。向こうは V3F と V5F を
  別々のイメージにして、メールボックスでつなぐ例です
- PIOC: 向こうは別の crate（pioc-programs）が作ったファームのイメージを読み込みます（`pioc` feature で使う）。
  こちらはスケッチの `.S` を clang で組みます（バイナリは内蔵しない方針）。

**両方にあるもの（向こうの状態）**: RCC、GPIO、EXTI、UART、SPI、I2C、ADC、タイマー（PWM・相補 PWM）、IWDG・WWDG、
CRC、RNG、Flash、AWU、USB FS / HS / SS（デバイス）、USB PD（X0・L1。H4 はレジスタの配置が違い外してある）、
ETH（V208 の 10M と H4 の内蔵 PHY は確認済み。V305 / V307 の MAC は作業中）、CAN（V2/V3・L1・H4）、SDIO（V4x7）。
V00X・CH641・CH643 などは ❓ が多く、こちらの方が確かめた系列は多い状態です。

## BLE（wchble）

- 無線をレジスタから直接動かします（WCH の閉じた .a は使わない）。上に bt-hci・trouble-host が乗ります。
- **CH585**: 広告・スキャン・接続・GATT（ペリフェラルとセントラル）まで、macOS と Android で通っています。
- **CH32V208**: RF の初期化・チャネル・送受信の基本・割込みまで。接続はまだです。
- CH592 は RF の煙試験だけです。CH583・CH572・CH595 はまだです。
- このコアでは BLE は作らない決定です（[unimplemented.ja.md](unimplemented.ja.md) の冒頭。ユーザー、2026-11-14）。
  V208 の BLE を検討し直すときは、wchble の `rf::family::ch32v208` が出発点になります。ベンダーのバイナリに頼らずに済みます。

## 拾える知見

- **DMA と WFI**: V003・V203 では、コアが WFI で眠っている間 DMA が進みません（ch32-hal の README）。2026-11-20 に測りました
  （CH32RVDma の 512 B〜1 KiB の語の写しを始めてすぐ WFI、SysTick で起きたときの残り）: **V003・V203・X035 は止まる**
  （約 1 ms 眠って 256 単位のうち 238、214、128 単位のうち 98 が残る）、**V006・L103 は進む**（WFI が DMA の完了の割り込みで
  数十 µs で終わる）。H417・V407 は WFI の前に写し終わるので判断できず。こちらのコアは `delay()` でも CH32RVDma の `wait()`
  でも WFI を使わず、`lightSleep()` は stop / standby で HCLK ごと止める（DMA も Serial も止まるのは文書のとおり）ので、
  影響はありません。スケッチが自分で WFI を使う場合だけの注意として CH32RVDma の README に書きました。
- **RAM_CODE_MOD**: 向こうは「実行中は変えられない、WCHISPTool で合わせる」としています。こちらは起動時にオプションバイトを
  書き換えて合わせています（RAM / Flash の配分のメニュー）。
- **H4 の DMA**: DMAMUX を device のデータの要求番号から引いています。こちらも同じ形（variant の表）です。
- **V305 / V307 の Ethernet の MAC**: 向こうは作業中。こちらは V307 で動いています。
