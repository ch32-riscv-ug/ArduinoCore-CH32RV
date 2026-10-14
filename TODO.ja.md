# TODO

決まったことは ADR や各文書に移し、ここからは消します。

## 製品ボード別のテンプレート（examples）

WeAct CH32X035 と UIAPduino Pro Micro V003 の2つは作った（libraries/CH32RV/examples/Board*）。板が増えたら同じ形で
足す（`board:` 宣言でその板だけビルドする）。

## USB

- EspUsbDevice の残りのクラス: オーディオ・ビデオ・USB ネットワーク（X035 で動くかを試してから）、SD カードの MSC。
  [docs/usb.ja.md](docs/usb.ja.md#今後の予定)
- Arduino IDE の書き込み（upload recipe）を `ch32rv isp flash --entry auto --app-device port:...` にする。
  ch32rv の discovery（9846fac、f20aee7）は `ch32rv-app://<USB シリアル>`（DFU ランタイムの名前が "CH32RV" のデバイス。
  モニタは同じデバイスの CDC で、ch32rv 側で確認済み）と `ch32rv-isp://<位置>`（1a86:55e0）を出す。その版を同梱したら
  platform.txt に `upload.tool.ch32rv-app` / `ch32rv-isp` と pluggable_monitor の対応を足す。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。
- **UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす**。今はブートローダー（1209:b003）だけで使っている。
  rv003usb をそのまま持ってくるか、同じような処理を自作するかから検討する（ライセンス、CH32RVUsbDevice と同じ API に
  載せられるか、V003 の Flash 16 KB / RAM 2 KB に収まるか）。
- V20x / V30x / L103 のデバイス対応、コントローラの実行時選択（V307 でリンクまで確認済み）、ホスト（`CH32RVUsbHost`）。
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
