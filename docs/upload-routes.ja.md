# 書き込み経路

系列と製品ボードごとに、どの経路で書き込めるか、書き込みモードへどう入るかをまとめます。
書き込みツールは同梱の `ch32rv` に一本化しています（[ADR-0008](adr/0008-upload-strategy.ja.md)）。
IDE port と経路の対応、discovery の設計は [oep-workflow.ja.md](oep-workflow.ja.md) §3 を参照してください。

文書基準: ch32rv 0.19.0（`tools/index/tools_ch32rv.json`）、ch32-device-data@773f929。
ch32rv の開発版では、下の表で「未確認」「不可」の系列も実機確認が済んでいます。次の ch32rv を採用するときに表を更新します。

## 経路の一覧

| 経路 | 必要なもの | IDE の Upload だけで完結 | ボードの手動操作 | アプリから書き込みモードへ | ch32rv | このコア |
|---|---|---|---|---|---|---|
| WCH-Link / WCH-LinkE | 外付け probe、デバッグ線 | ○ 書き込み、reset、起動確認まで | 不要 | 不要（デバッグモジュールが CPU を止める） | 実装済 | 結線済（`serial`、`wchlink://`） |
| OEP probe | OEP probe、デバッグ線 | ○ ch32rv の RAM loader で書く | 不要 | 不要 | 実装済 | 結線済（`oep://`、probe の serial port） |
| HID bootloader（UIAPduino、`1209:b803`） | USB のみ | 書き込みは自動、入るのは手動 | 必要: RESET を押したまま USB へ接続し、すぐ離す | 不可: bootloader が外部 reset の履歴を確認する | 実装済（CH32V003、16 KiB まで） | 結線済（`ch32rv-hid://`） |
| factory USB ISP（`4348:55e0`）、BOOT0 ピンで入る系列 | USB | 入るのが手動 | 必要: BOOT0（V103 は BOOT0 / BOOT1） | 未設計 | 未実装（`isp` は予約のみ） | 未結線 |
| factory USB ISP、X033 / X035 | USB | 初回は手動。`SerialUSB` を使うアプリからは 1200bps touch で入れる | 初回は必要: PC17（USB D+）を High にして電源投入 | 可能: `CH32RVUsbDevice` の 1200bps touch または DFU_DETACH → boot mode 設定 → software reset（X035 で実機確認）。DFU detach は未実装 | 開発版で実装（info / flash / verify / erase / reset、X035 で実機確認）。未リリース | upload recipe は未結線 |
| factory USB ISP、X305 / X315 | USB | 未確認 | 未確認 | 可能（予定）: X033 / X035 と同じ | 未実装 | 未結線 |
| factory UART ISP | USB-UART 変換 | — | — | V00x はアプリ経由、他は BOOT ピン | 未実装 | 対応しない |
| DFU（アプリの DFU runtime、CH32H41x） | USB のみ | ○ 動いているアプリの DFU_DETACH で入る | 不要 | 可能（DFU runtime は既定で付く） | 実装済（`boot dfu flash` / `boot dfu info`、ch32rv 0.22.0） | 結線済（CH32H41x の `ch32rv-app://` の口、platform.txt の ch32rv_dfu）。H417 の FS で arduino-cli から書けた（2026-10-21）。DETACH 直後に開けず失敗することがある（ch32rv 側で対応中） |
| 独自 bootloader（UF2 / UART） | 板による | — | — | — | `boot uf2` / `boot uart` は予約のみ | 対応しない（該当する板が出たら recipe を足す） |

デバッグ線の経路でも、アプリが SWIO を GPIO にした場合（`pinV32_DisconnectDebug()`）や深い sleep 中は接続できません。
その場合の復旧は、LinkE の power-off erase か、NRST を配線した OEP スロットでの `recover --method nrst` です。
1200bps touch、DTR/RTS、magic packet による自動切り替えは、どの経路にもまだありません。

## デバッグ線

CH32 のデバッグ線は 1 線の SWIO（SDI）と 2 線の RVSWD の 2 種類です。系列によって片方だけ、または両方に対応します。
出典は ch32-device-data の `index/debug_interfaces.csv` から ch32rv が生成した `crates/target/generated/debug_wiring.csv` です。

| 系列 | 対応する線 | SWIO / SWDIO | SWCLK |
|---|---|---|---|
| CH32V002 / V003 / V004 | 1 線のみ | PD1 | — |
| CH32V005 / V006 / V007、CH32M007 | 両対応 | PD1 | PB3 |
| CH32M030 | 両対応 | PA3 | PA2 |
| CH32V205、CH32V407 / V467、CH32X305 / X315 | 両対応 | PA13 | PA14 |
| CH32V103、CH32V203 / V208、CH32V303 / V305 / V307 / V317、CH32L103 / M103 | 2 線のみ | PA13 | PA14 |
| CH32X033 / X035 | 2 線のみ | PC18 | PC19 |

probe ごとの線の扱い:

| probe | 使える線 | 両対応の系列で使う線 |
|---|---|---|
| WCH-Link（CH549、E なし） | 2 線のみ | 2 線。1 線のみの V002 / V003 / V004 には接続できない |
| WCH-LinkE | 1 線、2 線 | LinkE が判断する |
| OEP probe | probe が宣言した `oep.wire.swio` / `oep.wire.rvswd` | 両方あれば V00x / M007 は SWIO、それ以外は RVSWD |

OEP で V005 / V006 / V007 / M007 以外の両対応系列（M030、V205、V407 / V467、X305 / X315）を 1 線で使うには、
probe 側を SWIO だけにする必要があります。

## 系列ごとの対応

凡例: 確認 = 実機で書き込みを確認、未確認 = 実装済みだが当該系列は未検証、不可 = ch32rv が拒否、— = 該当なし。

| 系列 | WCH-Link / LinkE | OEP probe | HID | factory USB ISP の入り方 | USB device |
|---|---|---|---|---|---|
| CH32V003 | 確認（LinkE 必須） | 確認（V003 治具、SWIO） | UIAPduino のみ確認 | — | — |
| CH32V006 | 確認（flash controller 直接書込、62 KiB で約 6 分） | 未確認 | — | — | — |
| CH32V002 / V004 | 未確認（LinkE 必須） | 未確認 | — | — | — |
| CH32V005 / V007、CH32M007 | 未確認 | 未確認 | — | — | — |
| CH32V103 | 確認（CH549 Link、LinkE） | 未確認 | — | BOOT0 / BOOT1 | FS |
| CH32V203 | 確認 | 未確認 | — | BOOT0 | FS |
| CH32V208 | 未確認 | 未確認 | — | BOOT0 | FS |
| CH32V205 | 確認 | 未確認 | — | 未調査 | FS / HS |
| CH32V307 | 確認 | 未確認 | — | BOOT0 | FS |
| CH32V303 / V305 / V317 | 未確認 | 未確認 | — | BOOT0 | FS |
| CH32L103 | 確認 | 確認（RP2350、RVSWD） | — | BOOT0 | FS |
| CH32M103 | 未確認（L103 と同じ手順） | 未確認 | — | BOOT0 | FS |
| CH32X035 | 確認 | 確認（ESP32-P4） | — | 電源投入時に PC17 = High、またはアプリから | FS |
| CH32X033 | 未確認 | 未確認 | — | 電源投入時に PC17 = High、またはアプリから | FS |
| CH32X315 | 確認（192 KiB の zero-wait 領域まで） | 未確認 | — | 電源投入時に BOOT 領域から起動（下記） | HS |
| CH32X305 | 未確認（X315 と同じ手順） | 未確認 | — | 電源投入時に BOOT 領域から起動（下記） | HS |
| CH32V407 / V467 | 不可（未検証のため ch32rv が除外） | 不可（loader の手順を作れない） | — | BOOT0 | HS |
| CH32M030 | 不可（未検証のため ch32rv が除外） | 使用しない（下記） | — | 無し | FS |

| 製品ボード | 標準の経路 | 他の経路 |
|---|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | HID bootloader。RESET を押したまま USB へ接続し、すぐ離す（[uiapduino-hid-upload.ja.md](uiapduino-hid-upload.ja.md)） | PD1 = SWIO に LinkE / OEP probe |

- OEP の「未確認」の系列は、同じ RAM loader を LinkE 経由で V003 / V006 / V103 / V203 / V307 / L103 / X035 / V205 / X315 に対して確認済みです。
- 初代の WCH-Link（CH549）の firmware 2.11 では、CH32V103R8T6 に書いたあと target が起動しませんでした（probe-rs で書いた記録）。ベンチでの確認は LinkE 2.22、CH549 Link 2.12 です。
- 「USB device」の列はハードウェアの有無です。このコアの USB device は X033 / X035 だけ結線済みです（[peripheral-support.ja.md](peripheral-support.ja.md)）。

## 書き込みで消す範囲（Erase Flash）

ツール → Erase Flash で選びます。

| 項目 | ch32rv に渡すもの | 動き |
|---|---|---|
| Keep Store（既定） | 何も渡さない（ch32rv の既定 `--erase keep-store`） | `CH32RV_FLASH_STORE` で確保した末尾を残す。WCH-Link と OEP では、ch32rv が退避して書き戻す（[flash-store.ja.md](flash-store.ja.md)） |
| All Flash | `--erase chip` | チップ全体を消す。WCH-Link、OEP、工場の ISP で効く |

- 値は書き込みの設定（`upload.erase_args`）なので、切り替えても再ビルドは起きません。
- H41x の DFU の経路は、チップ全体を消せません。All Flash を選ぶと、ch32rv が書き込みを断ります（exit 24）。
- UIAPduino の板は HID のブートローダーでしか書かないので、このメニューがありません。

## CH32M030 を OEP probe で書かない

ch32rv 0.19.0 は M030 にも 128 byte page で書く RAM loader の手順を作ります。
開発版では M030 の書き込みを実機確認済みなので、この節は次の ch32rv を採用するときに見直します。
ところが M030 の BUFLOAD は 64 bit 幅で、ch32rv の 1 word ずつ書く方式は、幅の広い buffer を持つ部品で page を壊すとされています。
M030 は実機で一度も書いていません。ch32rv 側で M030 を拒否するまでは、OEP の port を選んで M030 を書き込まないでください。

## 接続できなくなるイメージを試すとき（起動待ち）

書いたイメージがデバッガを受け付けない場合があります。たとえば、プローブが追従できないクロックへすぐ切り替えるものや、
デバッグの端子をほかの用途に使うものです。こうしたイメージは、NRST を押さえて接続しても、離した直後に
イメージのほうが先に動くので、止められないことがあります。

試すイメージには、ビルドで `CH32RV_BOOT_HOLD_MS` を付けます。電源投入とリセットのたびに、クロックを切り替える前、
端子に触る前に、その時間だけリセット時のクロックのまま待ちます。その間ならデバッガが止めて、書き直せます。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32L103:pnum=CH32L103C8T6,clock=96 \
  --build-property "build.extra_flags=-DCH32RV_BOOT_HOLD_MS=1000" <スケッチ>
```

- 既定では付きません。付けなければ、コードは 1 バイトも増えません。
- 待ちはリセット時のクロック（HSI かそれより遅い）で数えるので、指定より長くなります。L103 では 1000 ms の指定で
  約 2 秒です。
- 製品のイメージには付けません。起動がその分遅れます。

実例は CH32L103 です（2026-10-23、ch32rv 0.22.2）。

- OEP probe（RP2350）は、72 / 96 MHz で動いている L103 に接続できません（8 / 48 MHz なら接続できる）。
  接続に失敗するとチップがリセットされ、待ちの無いイメージは、プローブが止める前にまた 96 MHz に上がります。
  `--connect-under-reset` も通りませんでした。
- 待ちを付けたイメージは、失敗の直後（リセットされて待っている間）に、普通の `ch32rv flash` で書き直せました。
- WCH-LinkE（ファーム 2.22）は、同じイメージの L103 に 96 / 72 / 48 MHz のどれでも接続できました。L103 を
  72 / 96 MHz で試すなら、LinkE を使うか、OEP probe なら待ちを付けます。

## X033 / X035 / X305 / X315 の factory USB ISP

X033 / X035 は BOOT0 ピンを持ちませんが、PC17（USB D+）が BOOT 検出ピンです。
電源投入時に PC17 が High なら BOOT 領域の bootloader が起動します（CH32X035 データシート V2.3、ピン表の注 8）。
そのため、PC17 を抵抗経由で 3.3 V へ引くボタンかジャンパがあれば、
空のチップやデバッグ線を使えないボードでも、初回から USB で書き込めます。

例: WeAct CH32X035 CoreBoard V1.0 の DOWNLOAD KEY（K1）は、VDD33 から 5.1 kΩ（R1）を通して PC17 へつながっています
（WeActStudio.CH32X035CoreBoard の `Hardware/WeAct-CH32X035_CoreBoard_V10 SchDoc.pdf`）。
DOWNLOAD KEY を押したまま USB へ接続すれば factory USB ISP に入る回路です。実機での確認と、ボタンを離すタイミングの条件の確認はまだです。

RVSWD の PC18 / PC19 を I2C など別の用途に使うボードでは、この経路が通常の書き込み経路になります。
ただし ch32rv の `isp` は未実装です。

X305 / X315 は、電源投入時に BOOT 領域から起動する設定（`START_MD`）と、
「キー操作なしの USBFS ダウンロード」（`USBFSDLEN`）が option byte の既定で有効です（CH32X315 リファレンスマニュアル）。
ボタンなしで USB ISP に入れる可能性がありますが、実機では確認していません。

## USB を使う経路を足すときの前提

- このコアに USB（TinyUSB、[ADR-0012](adr/0012-usb-stack.ja.md)）を結線することが先です。X033 / X035 は `CH32RVUsbDevice` で結線済みです（[docs/usb.ja.md](usb.ja.md)）。
  これがないと、アプリから書き込みモードへ入れません。
- factory USB ISP は WCH 独自の protocol で、USB DFU class ではありません。書き込みは ch32rv の `isp` が担当します。

### アプリの USB から書き込みモードへ入る

USB DFU class（DFU 1.1）は、書き込み中の DFU mode と、アプリ実行中の run-time mode を分けて定義しています。
run-time mode の DFU interface はアプリの USB に CDC と並べて載せられるので、アプリは CDC + DFU の複合 device として起動できます。
host が DFU_DETACH を送ると、アプリが書き込みモードへ切り替えます。

| 入口 | host 側の操作 | 利点 | 欠点 |
|---|---|---|---|
| 1200bps touch（CDC） | CDC を 1200 bps で開いて閉じる | Arduino の慣習 | CDC の通信設定を合図に使う独自の約束 |
| DFU detach（DFU run-time interface） | DFU_DETACH request | 標準の request。interface の有無で、書き込みモードへ入れる device かを host が判別できる | interface が 1 つ増える |

切り替えた先の書き込みモードは 2 通りあります。

| 切り替え先 | 書き込み | flash の消費 | 向く系列 |
|---|---|---|---|
| factory USB ISP（boot mode 設定 → software reset） | ch32rv `isp` | なし | BOOT 領域へソフトウェアで切り替えられる系列（X033 / X035 / X305 / X315 等） |
| user flash に置いた独自の DFU mode bootloader | dfu-util（Arduino 組み込みの dfu-discovery） | bootloader の分 | factory USB ISP へソフトウェアで入れない系列、標準 tool で書きたい板 |

DFU detach から factory USB ISP へ入る場合、detach 後の device は DFU mode ではないので dfu-util では書けません。
ch32rv が detach と `isp flash` を続けて行う形になります。

アプリが USB を止めた、または壊れた場合の復旧には、アプリに依存しない入口が要ります。X033 / X035 では電源投入時の PC17、
BOOT0 ピンの系列では BOOT0、それ以外ではデバッグ線です。

### USB を持たない系列

V002–V007 / M007 では、UIAPduino と同じソフトウェア USB（rv003usb）の HID が代わりになります。

## 更新手順

ch32rv の version を上げたときは、次の内容をこの文書と [support-status.ja.md](support-status.ja.md) に反映します。

- `ch32rv db list --json` から作り直した `tools/index/ch32rv_chips.csv`
- ch32rv の CHANGELOG にある、実機確認した系列と probe
- ch32rv の `crates/target/generated/debug_wiring.csv` の変更
