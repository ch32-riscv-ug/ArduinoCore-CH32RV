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
  - L103 / M103、M030、V205: USB には既定の 8 MHz より高いシステムクロックが要る。ボード既定を変えるか USB 用の
    メニューにするかは、**実機ベンチがそろってから検討する**。
  - 第2世代 USBHS（V407 / V467、X305 / X315、V205）の dcd を新しく書く。**実機ベンチがそろってから検討する**。
    調査の下書きは work/usbport/others（git 管理外）。
  - H417: ボードとして入れてから USBSS。TinyUSB には SuperSpeed が無く（速度は FS / HS まで）、dcd を足すだけでは
    済まない。USBSS の dcd の新規作成に加え、TinyUSB 本体（速度、EP0 512 B、SET_SEL などの標準要求、SS の
    エンドポイントコンパニオン）とこのライブラリのディスクリプタ（SS 用、BOS の SS 能力）に手が入る。
  - ライブラリをボードマネージャーに同梱するか分けるか（下の「ライブラリの配布」）。
  - ベンダーの直接転送（USBHS の DMA で）。
  - NCM の IP 層（lwIP など）は単独で決めない。RJ45（Ethernet MAC を持つ V307 / V208 / V407 など）や将来の
    ほかのネットワークとどう統合するか（IP スタックを1つにして各インターフェースを載せるか、など）を全体として検討する。
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

## USB PD ソース（CH32RVUsbPd）の実機確認

ソース（`USBPDSource`、`CH32RVUsbPdSource`）は実装済み（ビルドとフレームの単体試験まで）。残りは実機での確認。
- X035 どうしの対向（シンク `USBPD` ⇔ ソース `USBPDSource`）。ソースにする WeAct の板は CC の 5.1 kΩ（R4 / R5）を
  外す（回路図では SB2 / SB3 で PC14 / PC15 につながる Rd。実物で確認）。試験のスケッチは両方のコールバックを
  登録して明示的に true を返す。
- 確かめること: Rd の検出と向き（表裏）、固定と PPS の契約、Get_Source_Cap、Soft_Reset / Hard_Reset、PPS の途絶
  （15 秒）、Reject（名乗っていない電流）、取り外し、コールバックが false を返したとき（VBUS: 接続しない、電圧:
  Hard Reset で 5 V に戻る）。
- 実際の電圧が出ているかは範囲外（基板側の仕事）。

## 系列で限られているもの（2026-10-15 の調査と対応）

[peripheral-support.ja.md](docs/peripheral-support.ja.md) の ○ / ◐ の中身。

直したもの（実機で確かめていないものは、その旨を書く）:
- V205・X305 / X315 の Serial / Wire / SPI / PWM: 端子ごとの AF 番号（AFIO GPIOx_AFLR / AFHR）を書くようにした。
  ルート表は AF 番号ごとに端子の組み合わせを全部並べ、FIX メニューにも出る。**ビルドのみ**。V205 / X315 の実機で
  Serial・Wire・SPI・analogWrite の信号が端子に出ることを確かめる。
- X033 / X035 の IWDG: LSI が無く HSI / 1024 で動くので、その周波数（46.875 kHz）を使う。X035 で 1 秒のリセットを実測。
- Serial6〜9: V205 の USART6〜8、V30x の UART6〜8、V407 / V467 の USART6〜10（Serial9 まで）。**ビルドのみ**。
- PWM: TIM4 / TIM5 の既定ピンを追加（tone / Servo が使うタイマーは除く）。タイマー管理に TIM8〜10（V30x）/ TIM8（V4x7）。
- ADC の内部チャネル: `analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`。X035・L103・V103・V307 で実測
  （VDD 3.2〜3.4 V。温度は L103 25 ℃、V307 24 ℃、V103 46 ℃ — V103 はデータシートの V25 が 1.1〜1.6 V と広い）。
- **L103 / M103 の analogRead() が約 620 カウント低かった**: EVT と同じ VDDA / 2 のオフセット補正を入れた（L103 で実測）。
  V20x / V30x の EVT にも校正値の扱い（Get_CalibrationValue: 校正直後の RDATAR を 2048 から引く）があるが、V307 では
  その値が 1728 で、当てると VDD が 2.64 V になり不自然（当てない今は 3.19 V）。入れていない。外部の電圧計と照合して決める。
- X033 の送信ポーリングを X035 と揃えた。

残り（方針待ち・新しい API・実機が要る）:
- クロックは系列ごとに固定（HSI のみ）。L103 / M103・V205・M030 は 8 MHz で PLL 未使用、V00x は 24 MHz。USB の
  L103 / M103・V205・M030 と、PD の他系列（USBPD は 48 / 24 / 12 MHz が要る）もこの決定待ち（上の USB の項）。
- PD の他系列（L103 / M103、V205、X305 / X315、M030）: CC 端子と上のクロック。
- PWM の remap 先と CHxN、TIM8〜10 の PWM。
- 新しい API: スリープ、Flash の自己書き換え / EEPROM、WWDG、RTC、SPI のスレーブ、GPIO 以外の EXTI。
- `analogReference()` はハードウェアに基準電圧の選択が無い。

## ライブラリの配布

CH32RVUsbDevice / CH32RVTinyUSB / CH32RVUsbPd をボードマネージャーに同梱するか、別のライブラリとして分けるかを決める。
判断の目安は「CH32RV 向けのパッチや依存があるなら同梱、TinyUSB を使う他のボード（Pico など）でもそのまま使えるなら分離」。
今の依存（同梱寄り）:
- コアのフック（`loop()` / `delay()` からの `ch32rv_usb_task()`）、variant の生成データ（クロック許可、IRQ 番号）、
  `ch32rv_registers.h` を直接使う。
- TinyUSB は CH32RV 向けに手を入れた版（パッチ1つ、SDK の代わりのヘッダ、ドライバのラッパー、振り分け）。
- arduino-pico などは自分の TinyUSB（pico-sdk / Adafruit TinyUSB）を持つので、もう1つ入れると衝突する。
分けられる部分: クラスの API と実装（HID・MIDI・オーディオなど）の大半は TinyUSB のクラスの上にあり、系列に依存しない。
分けるなら「ボード共通の USB デバイス API」と「コアごとのポート層」に割る設計になる（EspUsbDevice との共通化も含めて）。

