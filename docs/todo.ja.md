# TODO(未対応作業の積み上げ)

文書基準日: 2026-08-20

「あとで拡張できる設計なら、まずは作りやすいもので整える」方針で進めるため、
**先送りにした作業をここへ積み上げる**。実装を簡略化するたびに必ず1行足すこと。

凡例: `[P0]`実装をblockする / `[P1]`初期release前 / `[P2]`将来
`(要判断)`はmaintainerの決定待ち、`(要実機)`はハードウェアが要る。

**`[x]`は「実装して検証した」であって「承認された」ではありません。**
実装が入っているが承認されていないものは[承認状態](approval-status.ja.md)に一覧があります。

---

## Milestone 1: 主要boardで`Serial.println()`が通る

受け入れtestは`tests/sketches/basic/serial_println/`にあり、現在は正しく失敗している
(`'Serial' was not declared in this scope`)。

- [x] UART HALと`HardwareSerial`の実装。割込み駆動のTX/RX ring buffer、AFIO remap適用込み
- [x] `SystemInit()`の実体化(HSI直結、SysTick 1kHz)。`wiring_stub.c`は削除
- [x] syscalls(`_write`→Serial、heap/stack衝突を防ぐ`_sbrk`、他はEBADF/ENOMEM)
- [x] **CH32V003実機で`Serial.println()`が通った**([実験0011](experiments/0011-milestone1-serial-on-v003.ja.md))。
      送受信・SysTick精度・`F_CPU`一致まで確認
- [x] **X035 / V203でも実機確認**。`Serial.println()`と`core_api`13 checkが両方でpass。
      これでrv32ec/rv32imac、24/8/48MHz、32/64bit SysTick、8/16/24bit GPIO portを実機で通した
- [x] **CH32L103も実機確認**(`serial_println`)。ただし**routeをreset既定優先へ変える前**なので
      pinが変わっている。次に繋いだときに回し直すこと
- [ ] `[P1]` CH32V103とCH32V307で実機確認する。V103は**vector tableがジャンプ表の唯一のfamily** (要実機)
- [ ] `[P1]` V003 / V203 / L103をroute変更後のpinで回し直す (要実機)
- [ ] `[P2]` probe-rsがまれに`bulk read timed out`で書き込みに失敗する。
      `smoke.py`は1回だけ再試行し、**再試行したことを表示する**。頻度が上がるようなら原因を追う
- [ ] `[P1]` ADR-0006の「tickソース差し替え可能」をHALへ織り込む。
      現在`wiring_time.c`がSysTickを直接叩いており、RTOSへ渡す口が無い
- [x] **ring bufferのlost updateを修正**。`api/RingBuffer.h`は単一カウンタをpush/pop両方で
      read-modify-writeするため、ISRとsketchの2文脈から触るUARTでは壊れる。
      実機で文字化けとして観測([実験0013](experiments/0013-core-api-completion.ja.md))。
      lock不要のSPSC実装`ch32_ringbuffer.h`へ置換
- [x] ring bufferのサイズをbuild optionで変えられるようにした
      (`-DCH32_SERIAL_RX_BUFFER_SIZE=n` / `..._TX_...`。既定は64)。
      X035実測で256にすると`.bss`が+768バイト(4 USART分)
- [ ] `[P1]` `write()`は満杯でblockする。non-blockingにする手段が無い
- [ ] `[P1]` `micros()`が約71分でwrapする(AVRコアと同じ挙動)。64bit SysTickを持つfamilyでは避けられる
- [ ] `[P2]` SysTickのhardware auto-reload(`STRE`)を使う。現在はISRで`CNT`を巻き戻している
- [ ] `[P2]` `af-N` routeのper-pin alternate-function設定(V205/X305/X315)。
      現在コアが未対応で、これらのboardのSerialは未検証
- [x] `ltoa`/`ultoa`の実装(`cores/arduino/itoa.c`)
- [x] `dtostrf`: upstreamの`.c.impl`をincludeする`cores/arduino/dtostrf.c`を置いた
- [x] `Arduino.h`から`api/ArduinoAPI.h`をinclude。**`api/`はinclude pathへ入れず`api/`付きで書く**規律を維持
- [x] `F_CPU`とHCLKの一致を担保。compile時は
      [tests/test_clock_prescaler.py](../tests/test_clock_prescaler.py)が
      両符号化 × 19比でfield値を`_Static_assert`し、表現できない7比が`#error`になることも
      確認する(27 case、実機不要、0.4秒)。実機側はboard rateが`F_CPU`から計算されるため、
      **分周した状態でSerialが化けないこと**が一致の証明になる。
      X035実機で48→24MHz(`/2`)と48→16MHz(`/3`、linear符号化にしか無い値)を確認済み。
      `CH32_BUILD_PROPERTY=build.f_cpu=...`で再現できる
- [x] crt0→`setup()`/`loop()`到達をCH32V003実機で確認。`.data` copy、`.bss` zero fill、
      `.init_array`(C++大域constructor)まで全てpass([実験0010](experiments/0010-first-on-target-run.ja.md))
- [x] **実験0010を再現可能なtestにした**([tests/manual/crt0_probe/](../tests/manual/crt0_probe/))。
      書き込み後・reset前にprobe-rsでRAMを`0xDEADBEEF`で埋め、C++大域constructorが
      見た値をSerialで報告させる。**X035実機で全項目pass**
      (`.data`のcopy / `.bss`のzero fill / `.init_array` / `_ebss`の先にパターンが
      残っていること=対照)。順序が要点で、**書き込みalgorithm自体がRAMを使う**ため
      埋めるのはuploadの後。boardを載せ替えれば同じ1コマンドで回る
- [ ] `[P1]` 同じ確認を**L103/V20x/V103/V307**でも回す(要実機・board載せ替え)。
      `crt0_probe`がそのまま使える。variantごとのlinker scriptとvector tableの検証になる

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
- [x] `tests/manual/smoke/smoke.py`を`arduino-cli upload`経由へ移行。**出荷経路そのものを検証する**
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

## libc / heap

- [x] **libglossのsemihosting stubがheapとprintfを壊していた**
      ([実験0014](experiments/0014-libgloss-semihosting-stubs.ja.md))。
      `core.a`を`--start-group`で囲み、`_sbrk`を`ch32_sbrk.c`へ分離、
      `HardwareSerial.h`が`pins_arduino.h`を自分でinclude。X035実機で確認
- [x] `--specs=nano.specs`を既定にし、`menu.printf`で`%f`をopt-inにする案を実装(**未承認**、
      [承認状態 A-1](approval-status.ja.md))。ADR-0004が同じ形を提案しているが`Proposed`。
      `printf`sketchが48 KB → 7.1 KB、**CH32V003にも載るようになった**。X035実機で
      `printf=none`(空)/`printf=float`(`1.50`)を確認([実験0014](experiments/0014-libgloss-semihosting-stubs.ja.md))
- [ ] `[P1]` `menu.printf`の文言をdocumentへ。ADR-0004が求める
      「nanoの`%f`非対応はArduino利用者の既知の落とし穴」の明示がまだREADMEに無い
- [ ] `[P1]` `__stack_size`の既定が512バイト。`printf`は簡単に超える。
      variantかmenuで変えられるようにする。現状は`_sbrk`が`_heap_end`で止めるだけで、
      stack自体のoverflow検出は無い
- [ ] `[P2]` heapの断片化と`realloc`の実機確認。`heap_string`はまだ素直な経路しか見ていない
- [x] `_fstat`が`st_blksize`を設定するようにした(64)。newlibの`__swhatbuf_r`が読むので、
      未設定だとstdoutのbuffer sizeがstack上のゴミで決まっていた

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

- [x] **`SystemInit()`が`F_CPU`から分周器を決める**。`F_CPU`を目標HCLKとし、
      AHB prescalerは`CH32_HSI_HZ / F_CPU`から導く([cores/arduino/ch32_clock.h](../cores/arduino/ch32_clock.h))。
      これで**クロック変更はboards.txtの`f_cpu`だけ**になる。
      CH32系はprescalerの4bit fieldの符号化が2通りあり、一致するのは`/1`だけ:
      linear(`0x0..0x7`=`/1../8`、`/3`や`/5`もある)がV00x / M030 / X03x、
      pow2(`0x8`=`/2`、`/32`が無い)がV10x / V20x / V30x / V4x7 / L103 / V205 / X3x5。
      **全11 familyを各EVTヘッダの`RCC_HPRE_DIVn`で確認**し、推測は使っていない
      (`-DCH32_HPRE_LINEAR`をfamilyごとに生成)。
      表現できない比は`#error`。
- [x] **到達できない`F_CPU`は`#error`でコンパイル時に落とす**。
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

- [x] probe-rs v0.32.0をtool定義化(`tools/index/tools_probe_rs.json`)。
      6 host分の公式アーカイブと`.sha256`が揃っている唯一のbackend
- [x] `programmers.txt`と`platform.txt`の`tools.<t>.program.pattern`を実装。
      **`upload.pattern`ではなく`program.pattern`**を使う(実験0009で実測確認済み)
- [x] `sketch.yaml` profileの`programmer:`を有効化(`sync_profiles.py`が全profileへ出力)
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

- [x] **テスト計画を作成**([tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) / [英語](../tests/TEST_PLAN.md))。
      board階層は**未承認の提案**([承認状態 A-3](approval-status.ja.md))。
      自動/手動の切り分け、board階層(A/B/C/D)、ペリフェラル別の検証方法4種、
      Board Manager配布物としての検証項目
- [x] `tests/hardware/` → **`tests/manual/`**へ移動し、`chip_info.py`(chip/probe/port/FQBN判定)を追加。
      `test_`プレフィックスを付けないので`pytest`一括実行に混ざらない
- [x] `sketch.yaml`のprofile一覧を`sync_profiles.py`で生成。`compile_all.py`が
      **全sketch × 全profile boardをcompile**するのでCIに載せた
- [x] `install_check.py`にupgrade/rollback、受け入れsketchのcompile、
      アーカイブ内容の検査を追加。`gen_index.py`は`boards`を`boards.txt`から、
      `version`を`platform.txt`から取り、`--merge`でindexをappend-onlyに保つ
- [x] **release workflow**([.github/workflows/release.yml](../.github/workflows/release.yml))。
      tag `v<version>`のpushでRelease(アーカイブ) + GitHub Pages(index)を公開。
      公開中のindexを取得できなければ**publishを拒否**する(過去versionを消さないため)
- [x] **既存harnessを全部pytestに載せた。** `cd tests && uv run pytest`ひとつで
      生成物同期・API同期・割込み表・crt0等価性・compile matrix・sketch profile・
      sizebench・Board Manager installが回る。shell harnessは残し、pytestが呼んで
      markerを検証する形。CIも同じ経路へ。`slow` markerでcompile系を分離
- [x] **shell scriptを全廃してPythonへ移植**(6本、591行)。Windowsでの不具合3件が
      すべてshell由来(shebang非対応 / bash 3.2の構文 / パス区切り)だったため。
      移植後は各harnessの出力をshell版と突き合わせて**完全一致**を確認:
      compile matrix 122件、sketch profile 30通り、sizebench 30件、
      startup等価性 14 family/56 check、API sync、install checkの全marker。
      pytestは文字列マッチをやめ、**関数を呼んで戻り値をassert**する形に
- [x] CIで**ツール未取得によるskipを失敗扱い**にした(`CH32_TESTS_REQUIRE_TOOLS`)。
      skipは緑に見えるので、provisioningが壊れても気付けなかった
- [x] **WindowsのMAX_PATH超過を修正**。Board Manager installテストはpytestの
      作業ディレクトリ(`…\AppData\Local\Temp\pytest-of-<user>\pytest-0\harness0`、
      80文字)にtoolchainを展開するので、GCCが自前のheaderを開く時点で**284文字**に
      なり260を超えていた。GCCはinclude pathを
      `bin/../lib/gcc/…/../../../../riscv-none-elf/include/c++/…`と**解決せずに**開き、
      診断だけ正規化して出すため、実在する`bits/c++config.h`が「無い」というエラーになる。
      Windowsでは作業ディレクトリをドライブ直下(`C:\ch32t\`)に置くようにした
      (`CH32_TEST_TMP`で変更可)。222文字、余裕37文字。長さは
      `install_check.check_path_budget()`が事前に検査して落とす
- [x] 実利用側の余裕も測った。
      `C:\Users\<user>\AppData\Local\Arduino15\…\xpack-riscv-none-elf-gcc\14.3.0-1`
      だとユーザー名が**34文字**まで入る。Windowsのローカルアカウントは20文字上限なので
      実害は考えにくい。tool名から`xpack-`を落とせば40文字まで伸びるが、
      **maintainer判断で「とりあえず現状の名前のまま」**(2026-08-20)。
      公開済みindexと`platform.txt`の`{runtime.tools.*}`に関わるため、
      変えるなら別途判断する
- [x] **`tests/manual/`をpytestへ載せた。** 3 tool(`chip_info` / `uart_scan` /
      `smoke`)を「印字する`main()`」から**構造化データを返す関数**へ組み替え
      (`inventory()` / `scan()` / `resolve_bench()`+`run()`)、pytest caseを追加。
      配置は`gpio_loopback`と同じ**1 case = 1 ディレクトリ**(`<case>/<case>.py`)。
      設定はpytest optionではなく`tests/.env`の環境変数
      (`--port`と`--target`はpytest-embeddedが既に持っているため)。
      sketchのparametrizeは`manual/conftest.py`の`pytest_generate_tests`が行い、
      `pytest`はtest本体の中でimportする——CLIはPEP 723の`uv run`で動く必要があり、
      そこにpytestは入っていないため。CLIは対話用に残した。
      **実機(CH32X035C8T6)で両経路とも確認済み**
- [x] pytest化して**既存の実バグが4件出た**(いずれも表面化していなかった):
      `chip_info`が`probe_rs_targets.csv`の`#`行を落とさずDictReaderへ渡していて
      chip集合が常に空(=「CSVに無い」警告が一度も出ない)、
      `uart_scan`が存在しない`tests/hardware`を`sys.path`へ入れている、
      `uart_scan`が`detected_chip`/`boards_for`を未importで使用(`--board`省略で
      NameError)、`uart_scan`が`.tools`フォールバックを実装しておらず
      `CH32_GCC_BIN`/`CH32_TABLES`必須。`smoke`の`bench.json`も
      存在しない`tests/hardware/`を指していた
- [ ] `[P1]` **単体スクリプトの呼び出しをなくす。** 実行の入口は`pytest`ひとつにし、
      スクリプトを直接叩くのは相当の特殊事例だけにする。残っているもの:
      `tests/sketches/sync_profiles.py`、`tools/index/fetch_tools.py`、
      `tools/generate/generate.py`(いずれも生成・取得系で、testではない)
- [x] **profile経由のbuildをCIへ追加**
      ([tests/test_sketch_profile_build.py](../tests/test_sketch_profile_build.py))。
      `arduino-cli compile --profile`は**index経由**でplatformを解決する経路で、
      compile sweep(`--fqbn`+作業ツリー)ともinstall test(index install+`--fqbn`)とも別。
      利用者に案内しているのはこの経路。**30通り / 72秒**、`install-test` jobへ相乗り
      (index生成とtoolchain取得という高い部分を共有するため、3 OSぶん増えるのは72秒×3)。
      pytest-embeddedの`--run-mode build`はbuild後にtest本体をskipするだけなので
      検証内容は同じだが、あれは`--profile`をコマンドラインで要求し、かつ
      **未公開のindex URL**へ取りにいくため使わなかった。
      profileのindex URLはloopbackで配信し、`sketch.yaml`は**コピー側だけ**書き換える
      (実行中にコミット済み生成物を書き換えると、中断時にツリーが汚れるため)
- [x] **Windows install失敗の原因を特定**。probe-rsのWindows zipが平坦で、
      arduino-cliは単一root directoryを要求する。平坦/root付きの両方のzipを作って
      arduino-cliへ食わせ、前者だけが落ちることを実証
- [x] **WindowsでBoard Manager installができない原因を特定し、方針を決定**。
      probe-rsのWindows zipが平坦で、arduino-cliは単一root directoryを要求する。
      indexからWindows entryを削っても回避できない。方針は
      [ADR-0011](adr/0011-tool-mirror-repository.ja.md)(1ツール1repositoryでミラー、
      `mirror-probe-rs`、取り込み自動・採用手動)。中身は実装済みでローカル検証も通過
- [x] **`mirror-probe-rs`を公開し、tool配布をそちらへ切り替えた**
      ([ADR-0011](adr/0011-tool-mirror-repository.ja.md) `Accepted`)。
      ミラーURLからのclean installが通ることを確認し、CIの`install-test`へ
      windows-latestを復帰。**Windowsのinstall問題は解決**
- [x] `mirror-probe-rs`の失敗通知はGitHubの標準通知(Actions失敗メール)で受ける。
      専用の通知先は設けない
- [ ] `[P1]` Board Manager index を実際に公開する。workflowは用意したが未実行で、
      `sketch.yaml`の`platform_index_url`は未公開URLのまま
- [ ] `[P1]` `libraries/`(SPI/Wire)同梱後、installした状態で`#include <SPI.h>`が
      解決されるかを`install_check.py`へ追加
- [x] **testに要るものを`<repo>/.tools`へ集約**(`tools/index/fetch_tools.py`)。
      環境変数なしで全harnessが回るようになった。版は`tools/index/tools_*.json`
      (package indexの正本)から取り、SHA-256照合つき。device-dataは`boards.txt`の
      locked commitでcheckout。`CH32_*`は上書き用として存続。CIも同じ経路へ移行
- [x] `tests/sizebench/sizebench.py`が参照していた`tests/startup/crt0_ch32.S`と
      `tests/platform/`は既に存在せず、**harnessが動いていなかった**。正本
      (`cores/arduino/`)を指すよう修正。CIには載っていない
- [ ] `[P1]` 実機testを回すrunnerの用意。**WCH-LinkEを同時に1台しか使えない**ため、
      別ホスト(Raspberry Pi等)にboard farmを置く案を検討
      ([TEST_PLAN](../tests/TEST_PLAN.ja.md)の選択肢b)
- [x] `manual/gpio_loopback/` を追加。ジャンパ1本でレベル / pull-up / pull-down /
      別ポートへのEXTI / PWM dutyを見る。`manual/<case>/<case>.py`規約の最初の実例。
      **compileのみ確認、実機未実行**(padの配線が分からないと駆動できないため)
- [x] `smoke.py --sketch all` を追加。boardを差し替えたら1コマンドで全sketchを一巡し、
      最後に表を出す。合否は各`test_<name>.py`のリテラルから取り、加えて
      「出力に`FAIL`が無い」「`failures=0`」の一般規則を適用する
- [x] **手動testのpinを`.env`で上書きできるようにした**。既定値はsketch内に残しつつ、
      `tests/.env`の`CH32_LOOPBACK_OUT=PA0`のようなpad名を`conftest.py`が
      `env_config.h`へ変換する(`tests/manual/env_config.py`)。
      実行は`uv run --env-file .env pytest ...`
- [x] `--board`を省略可能にした(**未承認**の既定変更、[承認状態 A-4](approval-status.ja.md))。probe-rsが型番を読み`boards.txt`から逆引きするので、
      焼く対象と焼く相手が食い違うことが原理的に起きない。明示すると主張になる。
      `--pnum detect`で実SKUを選べる
- [ ] `[P1]` `gpio_loopback`を実機で回す (要実機・要ジャンパ)
- [ ] `[P2]` pytest profileもchip検出から選べるようにする。現在は`--profile`で人が指定する。
      pytest-embedded-arduino-cli側の対応が要るかもしれない
- [ ] `[P2]` `.env`の項目を増やす(serial port、probe serial等)。現在はCLI flagのみ
- [ ] `[P2]` `tests/manual/README.ja.md`のSerial pin表が手作業。variantから生成する
- [ ] `[P2]` sketchの`build_opt.h`がarduino-cli 1.3.1で効かない。
      `compiler.<lang>.extra_flags`をrecipeへ入れても`build.opt.path`が設定されず、
      `@<file>`がコマンドラインに現れない。原因未特定。
      `--build-property compiler.cpp.extra_flags=...`は効く
- [ ] `[P2]` arduino-cliへbug報告: sketch profileに`platforms:`が無いとpanicする
      (`internal/arduino/sketch/profiles.go:125`、1.3.1で確認)
- [ ] `[P2]` host contract test(Q-016)。upstream ArduinoCore-APIの`test/`を固定commitでcloneして使う

## 決定待ち(ADR)

- [ ] `[P0]` ADR-0001〜0009はすべて`Proposed`。大きい順に確認して`Accepted`にする
- [ ] `[P0]` Q-001: 対象boardの確定。**X035が主**だが、I2C/USB/SWDを同時に使うにはC8T6(LQFP48)以上が要る
- [ ] `[P1]` Q-013: 内部HAL contract。[旧コア監査](legacy-audit.ja.md)は境界の観測データとして使い、構造は踏襲しない
- [ ] `[P1]` Q-019: コア拡張(`Serial.printf()`等)の置き場所。前コアは`api/Print.h`にpatchを当てていた
- [ ] `[P2]` Q-017: 公開FQBN / packager ID / architecture ID
