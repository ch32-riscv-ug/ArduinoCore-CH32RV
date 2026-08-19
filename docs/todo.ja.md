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

- [x] UART HALと`HardwareSerial`の実装。割込み駆動のTX/RX ring buffer、AFIO remap適用込み
- [x] `SystemInit()`の実体化(HSI直結、SysTick 1kHz)。`wiring_stub.c`は削除
- [x] syscalls(`_write`→Serial、heap/stack衝突を防ぐ`_sbrk`、他はEBADF/ENOMEM)
- [x] **CH32V003実機で`Serial.println()`が通った**([実験0011](experiments/0011-milestone1-serial-on-v003.ja.md))。
      送受信・SysTick精度・`F_CPU`一致まで確認
- [ ] `[P0]` X035 / L103 / V20x でも実機確認する。runnerは`tests/hardware/smoke.py`に用意済み (要実機)
- [ ] `[P1]` ADR-0006の「tickソース差し替え可能」をHALへ織り込む。
      現在`wiring_time.c`がSysTickを直接叩いており、RTOSへ渡す口が無い
- [x] **ring bufferのlost updateを修正**。`api/RingBuffer.h`は単一カウンタをpush/pop両方で
      read-modify-writeするため、ISRとsketchの2文脈から触るUARTでは壊れる。
      実機で文字化けとして観測([実験0013](experiments/0013-core-api-completion.ja.md))。
      lock不要のSPSC実装`ch32_ringbuffer.h`へ置換
- [ ] `[P1]` ring bufferが64バイト固定。`write()`は満杯でblockする。
      サイズをvariantかbuild optionで変えられるようにする
- [ ] `[P1]` `micros()`が約71分でwrapする(AVRコアと同じ挙動)。64bit SysTickを持つfamilyでは避けられる
- [ ] `[P2]` SysTickのhardware auto-reload(`STRE`)を使う。現在はISRで`CNT`を巻き戻している
- [ ] `[P2]` `af-N` routeのper-pin alternate-function設定(V205/X305/X315)。
      現在コアが未対応で、これらのboardのSerialは未検証
- [ ] `[P0]` `ltoa`/`ultoa`の実装(upstreamは`api/itoa.h`で宣言のみ)
- [ ] `[P0]` `dtostrf`: upstreamの`api/deprecated-avr-comp/avr/dtostrf.c.impl`をincludeする`.c`を1本置く
- [ ] `[P0]` `Arduino.h`から`api/ArduinoAPI.h`をincludeする。**`api/`はinclude pathへ入れず、必ず`api/`付きで書く**(samd/renesas/mbedと同じ規律。arduino-cliが渡す`-I`はcoreとvariantのみなので現状のまま成立する)
- [ ] `[P1]` `F_CPU`と実際のSYSCLKの一致をtestで担保する(不一致だとSerialが化ける)
- [x] crt0→`setup()`/`loop()`到達をCH32V003実機で確認。`.data` copy、`.bss` zero fill、
      `.init_array`(C++大域constructor)まで全てpass([実験0010](experiments/0010-first-on-target-run.ja.md))
- [ ] `[P1]` 同じ確認をX035/L103/V20xでも回す(実機あり)。variantごとのlinker scriptとvector tableの検証になる

## 割込みとvector table

- [x] **ベクタテーブルをFLASH先頭へ移した**。QingKe V2は`mtvec`のベース下位ビットを捨てるため、
      アドレス8に置いた状態では全割込みが0番地へ飛んで1msごとに再起動していた
      ([実験0011](experiments/0011-milestone1-serial-on-v003.ja.md))
- [x] `compare.py`に`_vector_base == 0`のcheckを追加。
      **中身は合っているのに使われていない**状態をCIが見逃していた
- [x] `attachInterrupt`/`detachInterrupt`(EXTI)の実装。**vector分割の2方式(`EXTI7_0`系と
      `EXTI0..4`系)は生成物`exti_<variant>.h`から吸収**するので、family追加でコード変更が要らない
- [ ] `[P1]` X033/X035のEXTI線16〜23(`EXTI25_16`)。`AFIO_EXTICR`の追加wordが要る
- [ ] `[P1]` V003のvector tableに**spec外の非ゼロword**が1つある(EVT側にも同じものがある)。
      slot 39相当で実害は無いが出所を確認する
- [ ] `[P2]` 割込み優先度(`PFIC IPRIOR`)を触っていない。全てreset既定のまま

## 書き込み
- [x] 実機ボード取り違えのガード。`probe-rs info`が返すchip名と`--board`が
      食い違ったら書き込む前に止まる(ベンチはboardを差し替えるので実際に踏んだ)


- [x] `programmers.txt`と`program.pattern`の実体化。`arduino-cli upload --programmer wch-link`が
      **CH32V203実機で通った**([実験0012](experiments/0012-probe-rs-upload-toolchain.ja.md))
- [x] `tests/hardware/smoke.py`を`arduino-cli upload`経由へ移行。**出荷経路そのものを検証する**
- [ ] `[P1]` Windows / macOSでの書き込み確認(要実機)
- [ ] `[P1]` probe選択を`sketch.yaml` profileから渡せるようにする。
      現在は`--upload-property upload.probe_args=...`のみで、profileには書けない
- [ ] `[P2]` `upload.probe_args`にflag全体を書かせている。空のときに引数を消す方法が他にない

## コアAPI

- [x] ArduinoCore-APIが宣言する未実装関数11件を実装
      ([実験0013](experiments/0013-core-api-completion.ja.md))。
      `analogRead` / `analogWrite` / `attachInterrupt` / `detachInterrupt` /
      `shiftIn` / `shiftOut` / `pulseIn` / `pulseInLong` / `random` / `randomSeed` /
      `digitalPinToInterrupt`。`tone`/`noTone`はstub
- [x] 配線不要の自己検証sketch`tests/sketches/basic/core_api/`を追加。
      **CH32V203実機で13 check全passを確認**
- [ ] `[P1]` `tone()`が無音stub。timer channelの排他管理が要る
- [ ] `[P1]` ADC分解能(`CH32_ADC_BITS`)はdatasheet由来。**実機で確認する** (要実機)
- [ ] `[P1]` `analogWrite`のPWM周波数が1kHz固定。Arduino慣例には合うが変更手段が無い
- [ ] `[P2]` ADC2以降を使えるようにする。現在ADC1のみ
- [ ] `[P2]` X305/X315のPWM。timerもper-pin AF方式でdefault routeが無い
- [ ] `[P2]` `SPI`/`Wire`ライブラリ。Tier Aの要件([project-scope](project-scope.ja.md))

## Serial

- [ ] `[P1]` **SDI printfを独立したSerialクラスとして公開する**(`SerialSDI`等)。
      WCH-Linkのdebug data register(`0xE0000380`/`0xE0000384`)へ7バイトずつ渡す方式で、
      **UART配線が一切要らない**。hostは`minichlink -T`が読む。
      旧コアとWCH公式はどちらも`_write`の`#if`で切り替えるだけだったが、
      **Streamとして分けておけば両方同時に使える**。
      2026-08-20に生波形で試したが同梱minichlink(`-T`)では読めず、上流buildが要る
- [ ] `[P1]` `Serial`の実体をboardごとに差し替えられるようにする。
      series生成の既定は「全型番に出ているUSART」だが、実boardの配線は別
      (X035 EVTはWCH公式・旧コアとも**USART1/PB10**を使う)

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
- [x] `pins_arduino.h`の本実装。[ADR-0010](adr/0010-pin-numbering.ja.md)の`(port<<5)|bit`方式で
      seriesの全pad名・ポート別validity mask・`ANY`共通padマスクを生成。Blinkのサイズが
      117 SKU全てでbaselineとバイト一致し、**テーブルが生成されない**ことを実証
- [x] `A0`等アナログエイリアスのADCチャネルマップ生成。**ADC1のみ**採用(X305/X315は
      ADC1〜ADC4で同じチャネル番号が別padに出るため`A<n>`が一意にならない)
- [ ] `[P1]` X305/X315のADC2〜ADC4を表現する。現在`A<n>`はADC1のみで、他instanceのpadは
      アナログとして到達できない
- [ ] `[P1]` `LED_BUILTIN`がgeneric boardでは実体のないplaceholder(seriesの全型番に共通する
      最小pad)。製品名boardを足すときに実体へ差し替える
- [ ] `[P2]` `NON_PORT_PADS`(`ANT`/`HO3`/`ISP1`/`LED0`/`MDITP`等)を`generate.py`で手管理している。
      device-data側にGPIOポートbitか否かのフラグが入れば不要になる
- [ ] `[P1]` device-dataのsignal名正規化。**X035とV003が最も未正規化**(`SCL`/`MISO`/`T1CH1`のような裸名)。
      V203は`I2C1_SCL`、V006は`I2C_SCL`と表記が揃っていない
- [x] X035エラッタのvariant表現(`x035-pc10-pc17-bonded`、ADR-0010のDecision 4)。
      **PC10/PC11はそもそもpadとして出ていない**ので、pad属性ではなく除外リスト
      `CH32_UNUSABLE_PINS`として生成した。errata idの存在をgenerate.pyが検証する
- [ ] `[P1]` `CH32_UNUSABLE_PINS`をcore側で実際に弾く(現在は宣言のみ。`pinMode`実装時に対応)
- [x] `[compile only]`表示を**probe-rsのcoverageから自動導出**するようにした。
      手書きフラグではCH32M103が漏れていた(7 series / 27 entryが対象)
- [ ] `[P2]` その7 series(M030/M103/V205/V407/V467/X305/X315)にupload経路を用意する。
      probe-rsにtargetが無く、**実物がほとんど流通していないチップ**でもある。wlink併用で埋まる
- [x] **probe tool定義(Q-040)をprobe-rs 0.32.0で実装**した([実験0012](experiments/0012-probe-rs-upload-toolchain.ja.md))。
      `tools/index/tools_probe_rs.json`(GitHub Releases直リンク、再ホストなし)、
      `programmers.txt`の`wch-link`、`probe_rs_targets.csv`からのchip名生成。
      clean installで`.tar.xz`の展開と`probe-rs --version`まで検証
- [ ] `[P1]` probe-rsのversion追随方針を決める。現在0.32.0固定で更新は手動。
      chip listが増えると`[compile only]`が減るので、更新にはboards.txt再生成が伴う
- [ ] `[P2]` minichlinkは補助扱い。同梱`minichlink-2982dfd`は`-l`もCH32L103も持たない。
      使うなら上流buildを固定すること(libusb-1.0の開発headerが要る)
- [ ] `[P1]` fixture inventoryに**probeの種別とfirmware version**を記録する。
      初代WCH-Link(CH549)とWCH-LinkEは電源制御とV003単線SDIの可否が違う
- [ ] `[P1]` HIL runnerのsetupに`1a86:8010`のudev rule導入を含める(無いと`LIBUSB_ERROR_ACCESS`)
- [ ] `[P2]` Serial監視にCH340(`1a86:7523`)系adapterを使わない方針を明文化する。
      **USB serialを持たず複数台を区別できない**。CH343(`55d3`)はuniqueなserialを持つ
- [ ] `[P2]` 製品名board(`WeAct CH32X035 CoreBoard`等)の追加。series boardと共存できる
- [x] **CH32V103対応**。vector tableのj命令形式に対応し、24 board / 122エントリへ。
      `import_vectors.py`がtableの形式(`word`/`jump`)を記録し、`crt0`は`CH32_MTVEC_MODE`で
      切り替える(V103は`1`、他は`3`)。`CH32_INTSYSCR_INIT`はV103が書かないのでoptional化。
      `compare.py`にj命令のdecodeを足し、**等価性harnessで59 entry一致を確認**(14 variant)
- [ ] `[P1]` CH32V103の`NRST` padがdevice-dataでport名を持たない。GPIOとして使えるなら
      upstreamへ報告する(現在は`NON_PORT_PADS`で除外)
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
