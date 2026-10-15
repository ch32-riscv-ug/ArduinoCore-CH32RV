# TODO

決まったことは ADR や各文書に移し、ここからは消します。

## 製品ボード別のテンプレート（examples）

WeAct CH32X035 と UIAPduino Pro Micro V003 の2つは作った（libraries/CH32RV/examples/Board*）。板が増えたら同じ形で
足す（`board:` 宣言でその板だけビルドする）。

## USB

- **CH32RVUsbDevice は FS / HS / SS（H417 の USBSS）まで動くライブラリとして、EspUsbDevice の全機能を実装する。**
  済み: 速度ごとのディスクリプタ（FS / HS）、V103 / V20x（USBFS）/ V30x（USBFS・USBHS を実行時に選ぶ）、NCM、
  オーディオ、ビデオ、SD カードの MSC。残り:
  - 新しい系列の実機確認（ベンチの DUT の USB 端子を配線してから）。HSI から作る 48 MHz の精度、V103 の EP4〜7 の
    自動トグル、HS の PHY と HSE。
  - L103 / M103、M030、V205: USB には既定の 8 MHz より高いシステムクロックが要る（ボード既定の変更は方針の確認待ち）。
  - V20x の USBD（fsdev）: USBFS と実行時に選べるようにする（USBD しか無い V203F6P6 / G6U6 のため）。
  - 第2世代 USBHS（V407 / V467、X305 / X315、V205）の dcd を新しく書く。下書きは work/usbport/others（git 管理外）。
  - H417: ボードとして入れてから USBSS（TinyUSB に無い）。
  - ベンダーの直接転送（USBHS の DMA で）。NCM の IP 層（lwIP など外部ライブラリ。方針の確認待ち）。
  - ほかの系列の `enterBootloader()`（工場出荷 ISP への入口。今は X035 / X033 だけ）。
  - デバッガ復帰時の `SystemInit()` のやり直しで USB のクロック設定が消える件の対策。
  - ファームウェア更新のクラス（本物の DFU、MSC での更新）は、工場出荷の ISP で書く今の方針と合わせて別に決める。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。
- **UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす**。今はブートローダー（1209:b003）だけで使っている。
  rv003usb をそのまま持ってくるか、同じような処理を自作するかから検討する（ライセンス、CH32RVUsbDevice と同じ API に
  載せられるか、V003 の Flash 16 KB / RAM 2 KB に収まるか）。
- ホスト（`CH32RVUsbHost`）。
- **USB デバイスの実機試験のホスト役を PC から別のマイコンに替える**。試験は `tests/instrumented/usb`（`--case usb`）に
  あり、X035 で列挙・CDC / HID / MIDI の往復・`end()`→`begin()`・DFU_DETACH / 1200bps タッチでの ISP 移行を確かめている。
  ホスト役は今は PC（`PcUsbHost`）で、`ch32rv isp flash --entry auto` での書き込みの往復はまだ入っていない。
  - クラスを足すとき: 列挙・1往復・ほかのクラスとの同時宣言で足りる。アイソクロナス転送（オーディオ・ビデオ）は
    転送速度と取りこぼしも測る。
  - 系列を足すとき: dcd と PHY・クロックが系列ごとに違うので、大きめの双方向転送、抜き差し、サスペンド・レジューム、
    ブートローダーへの切り替えまで確かめる。
  - 接続先（ホスト役）を別のマイコンにする理由: 試験の条件を PC の OS やドライバに左右されないようにする（今のキーボード試験は HID のカーネルドライバを外して PC に入力が届かないようにしている）。
    - 当面は Raspberry Pi Pico。OEP から SWD で書き込み、本体の USB 端子は DUT のホスト専用にする（PIO-USB は不要）。
      試験の指示と結果は OEP 経由でやり取りする。
    - X035 のホストドライバを書いたら、X035 もホスト役にできる（`CH32RVUsbHost` の試験を兼ねる）。
    - 電源のスイッチは追加しない。VBUS は流さず、DUT も Pico も 3V3 で動かす（Pico は 3V3 端子に入れて 3V3_EN を GND に
      つなぐか、VSYS に 3.3 V を入れる。採用前にデータシートで確かめる）。
      CH32RVUsbDevice も TinyUSB の X035 用ドライバも VBUS を見ないので、D+ のプルアップだけで接続になる。試験の
      スケッチは `config.selfPowered = true` にする。
    - 抜き差しは、DUT の `device.end()` → `device.begin()`（D+ のプルアップを外して付け直す。ホストからは抜き差しと
      区別がつかない）と、Pico 側のポートのリセットで代える。サスペンド・レジュームは Pico が SOF を止めて起こす。
      電源の入れ直しは、OEP からのリセット（NRST / デバッグ線）で代える。
    - VBUS の有無に依存する動作は試せない（今のライブラリには無い。VBUS の検出を入れるなら別の確認が要る）。
