# TODO

決まったことは ADR や各文書に移し、ここからは消します。

## 製品ボード別のテンプレート（examples）

製品ボードごとに、使えるペリフェラルをなるべく全部初期化して使う雛形を examples に置く。同時に使えないものは、
コメントを外せば使えるようにしておく。

- **WeAct CH32X035 Core Board**: USB デバイス（CDC）、PD、UART、I2C、SPI、ADC、PWM、LED。I2C は USB か SWD の
  端子にしか出ていないので、その切り替えをコメントで示す。
- **UIAPduino Pro Micro CH32V003 V1.4**: UART・I2C・SPI（ハードウェアは各1系統）、ADC、PWM、LED（PC0）。
  SPI は既定の AUTO のままにし、メーカーのピン配置図にある2通りの route（NSS が PC1 か PC0 か）はテンプレートの
  コメントで示す。
- 板の数はいま2つ。増えたら同じ形で足す。

決まったこと: DFU ランタイムは USB デバイスの初期化で既定で付き、`config.dfuRuntime = false` で外せる（実装済み）。

## USB

- EspUsbDevice の残りのクラス（ベンダー〔バルク〕・WebUSB / MS OS 2.0 の設定、MSC、CCID。オーディオ・ビデオ・
  USB ネットワークは X035 で動くかを試してから）。[docs/usb.ja.md](docs/usb.ja.md#今後の予定)
- Arduino IDE の書き込み（upload recipe）を `ch32rv isp flash --entry auto --app-device port:...` にする。
  DFU ランタイムだけのスケッチ（tty が無い）を IDE のポートとして見せる discovery を ch32rv 側と相談する。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。
- **UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす**。今はブートローダー（1209:b003）だけで使っている。
  rv003usb をそのまま持ってくるか、同じような処理を自作するかから検討する（ライセンス、CH32RVUsbDevice と同じ API に
  載せられるか、V003 の Flash 16 KB / RAM 2 KB に収まるか）。
- V20x / V30x / L103 のデバイス対応、コントローラの実行時選択（V307 でリンクまで確認済み）、ホスト（`CH32RVUsbHost`）。
- **USB デバイスの実機試験を `tests/` に整備する**。X035 で手作業で確かめた内容（列挙、CDC / HID / MIDI / ベンダー HID の
  往復、複数クラスの同時宣言、DFU_DETACH・1200bps タッチ・`ch32rv isp flash --entry auto` での書き込みの往復）を1本に
  まとめ、クラスや系列を足したときに同じ試験を回せるようにする。今は `work/`（git 管理外）の使い捨てスクリプト。
  - クラスを足すとき: 列挙・1往復・ほかのクラスとの同時宣言で足りる。アイソクロナス転送（オーディオ・ビデオ）は
    転送速度と取りこぼしも測る。
  - 系列を足すとき: dcd と PHY・クロックが系列ごとに違うので、大きめの双方向転送、抜き差し、サスペンド・レジューム、
    ブートローダーへの切り替えまで確かめる。
  - 接続先（ホスト役）は PC ではなく別のマイコンにする予定（Raspberry Pi Pico などを検討中）。キーボードやマウスの
    試験で、PC に本物の入力が届かなくなる。
