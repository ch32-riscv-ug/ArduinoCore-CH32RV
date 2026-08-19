# TODO(未対応作業の積み上げ)

文書基準日: 2026-08-19

「あとで拡張できる設計なら、まずは作りやすいもので整える」方針で進めるため、
**先送りにした作業をここへ積み上げる**。実装を簡略化するたびに必ず1行足すこと。

凡例: `[P0]`実装をblockする / `[P1]`初期release前 / `[P2]`将来
`(要判断)`はmaintainerの決定待ち、`(要実機)`はハードウェアが要る。

---

## Milestone 1: 主要boardで`Serial.println()`が通る

受け入れtestは`tests/sketches/basic/serial_println/`にあり、現在は正しく失敗している
(`'Serial' was not declared in this scope`)。

- [ ] `[P0]` UART HALと`HardwareSerial`の実装。ADR-0006の「tickソース差し替え可能」を織り込む
- [ ] `[P0]` `SystemInit()`の実体化。現在`cores/arduino/wiring_stub.c`で空実装
- [ ] `[P0]` syscalls(`_write`最低限、`_sbrk`等)
- [ ] `[P0]` `ltoa`/`ultoa`の実装(upstreamは`api/itoa.h`で宣言のみ)
- [ ] `[P0]` `dtostrf`: upstreamの`api/deprecated-avr-comp/avr/dtostrf.c.impl`をincludeする`.c`を1本置く
- [ ] `[P0]` `Arduino.h`から`api/ArduinoAPI.h`をincludeする。**`api/`はinclude pathへ入れず、必ず`api/`付きで書く**(samd/renesas/mbedと同じ規律。arduino-cliが渡す`-I`はcoreとvariantのみなので現状のまま成立する)
- [ ] `[P1]` `F_CPU`と実際のSYSCLKの一致をtestで担保する(不一致だとSerialが化ける)
- [ ] `[P1]` crt0→`setup()`到達を実機で確認(現在は静的検査のみ) (要実機)

## クロック

**決定(2026-08-19)**: **初期は内蔵発振器(HSI)のみ**。HSEは将来の拡張とする。
値は**boards.txtの固定値**とし、クロックメニューは設けない。

メニューを後から足してもFQBNは壊れないことを実測確認済み
(menuキーを省略すると先頭に並べた項目が既定値として使われる)。
したがって「今は固定、必要になったらメニュー」で拡張性は失われない。

### 今やっておく拡張準備(これを守ればメニュー追加はboards.txtの行追加だけで済む)

- [ ] `[P0]` **`SystemInit()`は`F_CPU`を読んで分周器を決める**。周波数をハードコードしない
- [ ] `[P0]` **到達できない`F_CPU`は`#error`でコンパイル時に落とす**。
      F_CPUと実際のSYSCLKがズレるとSerialが化けるため、実行時に発覚させてはいけない

### Milestone 1の固定値: 「HSI直結、PLLなし」で全family統一

| family | HSI | f_cpu | 備考 |
|---|---:|---:|---|
| **CH32X035** | 48 MHz | **48 MHz** | 分周`/1`のみ。最大値がそのまま出る |
| CH32V003 / CH32V006 | 24 MHz | 24 MHz | 48MHzにはPLL×2が要る |
| V20x / V307 / L103 / M030 / V205 | 8 MHz | 8 MHz | 本来はPLLで逓倍すべき |

8MHzでもSerial 115200は分周比69.4(誤差0.6%)で成立するため、Milestone 1の目的は達成できる。

### 将来

- [ ] `[P1]` familyごとのPLL対応。boards.txtの`f_cpu`を変え、そのfamilyの`SystemInit`にPLL設定を足す。
      優先度が高いのは8MHz HSI系(V20x/V307/L103/M030/V205)
- [ ] `[P2]` クロックのメニュー化(STM32duino型)。既定値を現在の固定値と同じにすればFQBN互換は保たれる
- [ ] `[P2]` HSE対応。boardごとの水晶有無・周波数をvariantへ持たせる。**X035はHSE非搭載のため対象外**

## ボード定義の生成

- [x] 割込みvector tableを`tools/generate/interrupts/interrupts.csv`へ配置(13 variant / 904 slot)。
      `generate.py`が`vectors_*.inc`を生成、`import_vectors.py --check`をCIへ追加
- [ ] `[P1]` **公開価値が出たら`ch32-device-data`へ移送する**。トリガは「2つ目のconsumerが現れたとき」
- [x] 生成器をseries board(23 board / 117エントリ)へ拡張。ANY先頭・`[compile only]`表示込み([ADR-0005](adr/0005-board-structure-and-fqbn.ja.md)改訂)
- [ ] `[P1]` ハーネスと`FAMILY_CONFIG`のパラメータ二重管理を解消(片方を正本にするかCIで一致検証)
- [ ] `[P0]` `pins_arduino.h`の本実装。[ADR-0010](adr/0010-pin-numbering.ja.md)の
      `(port<<5)|bit`方式でseriesの全pad名を生成する。現在はplaceholder
- [ ] `[P1]` `A0`等アナログエイリアスのADCチャネルマップ生成
- [ ] `[P1]` device-dataのsignal名正規化。**X035とV003が最も未正規化**(`SCL`/`MISO`/`T1CH1`のような裸名)。
      V203は`I2C1_SCL`、V006は`I2C_SCL`と表記が揃っていない
- [ ] `[P1]` X035エラッタのvariant表現: `PC10`/`PC11`をoutput不可としてマークする
      (`x035-pc10-pc17-bonded`。PC16/PC17と内部結線)。ADR-0010のDecision 4
- [ ] `[P2]` `[compile only]`表示の6 series(V205/V407/V467/X305/X315/M030)にupload経路を用意する。
      probe-rsにtargetが無く、**実物がほとんど流通していないチップ**でもある。wlink併用で埋まる
- [ ] `[P2]` 製品名board(`WeAct CH32X035 CoreBoard`等)の追加。series boardと共存できる
- [ ] `[P2]` CH32V103対応。vector tableがj命令形式でharnessが除外中
- [ ] `[P2]` CH32H417対応。loadcode bootでharnessが除外中
- [ ] `[P2]` device-dataの`product_attributes.csv`の属性名揺れをupstreamへ報告
      (`usart`/`serial_port`/`communicationinterfaces`、1件は文字列逆順の`ecafretninoitacinummoc`)

## 書き込み(upload)

方針: **Board Manager経由でtoolを入れ、`arduino-cli upload`から実行する**。
xPack toolchainと同じ「GitHub Releases直リンク」方式([ADR-0002](adr/0002-toolchain-distribution.ja.md))。

- [ ] `[P0]` probe-rs v0.32.0をtool定義化(`tools/index/`に`tools_probe_rs.json`)。
      6 host分の公式アーカイブと`.sha256`が揃っている唯一のbackend
- [ ] `[P0]` `programmers.txt`と`platform.txt`の`tools.<t>.program.pattern`を実装。
      **`upload.pattern`ではなく`program.pattern`**を使う(実験0009で実測確認済み)
- [ ] `[P0]` `sketch.yaml` profileの`programmer:`を有効化(現在コメントアウト)
- [ ] `[P1]` probe-rs未対応familyのfallback: **CH32V205 / V407 / X315 / M030**。
      wlink併用か、probe-rsへのtarget追加貢献か (要判断)
- [ ] `[P1]` wlinkは`-d <INDEX>`しか持たない。**serial selectorの上流patch**(serialは既に読めている)
- [ ] `[P1]` udev rulesの配布と案内。ch32funの`minichlink/99-minichlink.rules`が参考。
      **開発機に未インストール**(LinkE 1a86:8010 / 8012、IAP 4348:55e0)
- [ ] `[P2]` minichlink対応(互換probe 6種とrv003usb BL)。**公式release assetが無く自前buildが要る**
- [ ] `[P2]` wchisp(USB/UART ISP)。GPL-2.0のため配布形態に注意
- [ ] `[P2]` UIAPduino等のboard固有BL

## Probe識別 / HIL

- [ ] `[P0]` **(要実機)** LinkE 2台接続で`probe-rs list`を実行し、serialが個体別に出るか確認。
      出れば`--probe VID:PID:Serial`で確定選択できる
- [ ] `[P1]` 「LinkEを同時に使えない」原因の切り分け。udev権限 / WSL usbipd / 選択機構のないtool、が候補
- [ ] `[P1]` `board-identify`にCH32 probeを追加(ESP32のMAC読み出しに相当するのはtarget UID読み出し、Q-042)
- [ ] `[P1]` **(要判断)** logic analyzerを16 channelにするか。
      v1.0の7周辺を同時に観測するには12本要り、**8chでは足りない**。部材とconnectorの変更コストが最も高い
- [ ] `[P1]` **(要実機)** fixture配線の確定(Q-050)。X035はSWD=PC18/PC19、USB=PC16/PC17、CC=PC14/PC15が固定
- [ ] `[P1]` X035のI2Cはロット依存で使えない個体がある(`x035-adc-ch-i2c-unavailable`、
      下から5桁目=0)。**fixture inventoryにロット番号を記録**し、ADC試験はch3/7/11/15を避ける
- [ ] `[P2]` 複数DUT化。1 LinkE + mux か 1 lane 1 LinkE かはprobe識別の結果次第(Q-043)

## テスト基盤

- [ ] `[P1]` CIへpytest sketch testを追加(`--run-mode build`)。
      ローカルindexの配信が要る。**現在はSerial未実装で失敗するため追加は実装後**
- [ ] `[P1]` Board Manager index の公開(GitHub Pages)。
      `sketch.yaml`の`platform_index_url`が未公開URLを指している
- [ ] `[P2]` arduino-cliへbug報告: sketch profileに`platforms:`が無いとpanicする
      (`internal/arduino/sketch/profiles.go:125`、1.3.1で確認)
- [ ] `[P2]` host contract test(Q-016)。upstream ArduinoCore-APIの`test/`を固定commitでcloneして使う

## 決定待ち(ADR)

- [ ] `[P0]` ADR-0001〜0009はすべて`Proposed`。大きい順に確認して`Accepted`にする
- [ ] `[P0]` Q-001: 対象boardの確定。**X035が主**だが、I2C/USB/SWDを同時に使うにはC8T6(LQFP48)以上が要る
- [ ] `[P1]` Q-013: 内部HAL contract。[旧コア監査](legacy-audit.ja.md)は境界の観測データとして使い、構造は踏襲しない
- [ ] `[P1]` Q-019: コア拡張(`Serial.printf()`等)の置き場所。前コアは`api/Print.h`にpatchを当てていた
- [ ] `[P2]` Q-017: 公開FQBN / packager ID / architecture ID
