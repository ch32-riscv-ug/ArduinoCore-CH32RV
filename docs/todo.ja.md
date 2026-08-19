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

- [ ] `[P0]` **(要判断)** 初期は全familyで内蔵発振器(HSI)固定にするか。
      HSEを使うと`f_cpu`がboard属性になりvariantに水晶情報が要る。X035はHSE非搭載のため制約ゼロ
- [ ] `[P0]` **(要判断)** familyごとの既定SYSCLK。X035は48MHz(HSI直、PLL不要)が自明。
      V003/V006は24MHz HSI→PLL×2で48MHz。8MHz HSI系(V20x/V307/L103/M030/V205)は個別確認
- [ ] `[P2]` クロックのメニュー化(STM32duino型)。需要が出てから
- [ ] `[P2]` HSE対応。boardごとの水晶有無・周波数をvariantへ持たせる

## ボード定義の生成

- [ ] `[P0]` 割込みvector tableをこのリポジトリへ正規化データとして置く(`tools/generate/interrupts/`)。
      13 variant / 904 slot / 実handler 775 / ユニーク名157。`generate.py`が`vectors_*.inc`を生成し、
      既存のstartup harnessが毎PR EVTと照合する
- [ ] `[P1]` **公開価値が出たら`ch32-device-data`へ移送する**。トリガは「2つ目のconsumerが現れたとき」
- [ ] `[P0]` `FAMILY_CONFIG`を10 familyへ拡張。march/mabi/CSR初期値は
      [tests/startup/run_check.sh](../tests/startup/run_check.sh)の表から移す
- [ ] `[P1]` ハーネスと`FAMILY_CONFIG`のパラメータ二重管理を解消(片方を正本にするかCIで一致検証)
- [ ] `[P1]` variant(pin map)生成。Arduinoピン番号設計の合意が前提(Q-011)
- [ ] `[P1]` device-dataのsignal名正規化。**X035とV003が最も未正規化**(`SCL`/`MISO`/`T1CH1`のような裸名)。
      V203は`I2C1_SCL`、V006は`I2C_SCL`と表記が揃っていない
- [ ] `[P1]` X035エラッタのvariant表現: `PC10`/`PC11`をoutput不可としてマークする
      (`x035-pc10-pc17-bonded`。PC16/PC17と内部結線)
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
