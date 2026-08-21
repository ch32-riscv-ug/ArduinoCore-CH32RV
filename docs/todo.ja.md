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

## 対応範囲の目安: UNO入門キット相当

**方針(2026-08-20、ユーザ指示)**: UNO用入門キット
(Freenove Ultimate Starter Kit、`~/dev_wch/Freenove_Ultimate_Starter_Kit-master`)は
「このくらいのことができてほしい」を示す**ユースケース例**であって、
キットのlibraryが全部動くことを目標にはしない。
キットは**外部資料として参照するだけ**で、sketchもlibraryもこのrepositoryへ取り込まない。

**基本クラスは優先度に関係なく全部実装する**。実装できるものから順に入れ、
**実機検証は後でまとめて回す**。

50 sketchが呼ぶcore APIを数えた結果、coreに足りないものは2つしかない。

| キットが使うAPI | 状態 |
|---|---|
| `pinMode`(71) / `digitalWrite`(60) / `digitalRead`(10) | 実装済 |
| `Serial.begin`(16) / `print`(69) / `println`(36) / `available` / `read` / `parseInt` | 実装済 |
| `analogRead`(11) / `analogWrite`(20) | 実装済。ただしADC分解能は実機未確認、PWMは1kHz固定 |
| `shiftOut`(6) / `pulseIn` / `delayMicroseconds` / `attachInterrupt` / `millis` / `random` | 実装済 |
| **`tone`(3)** | **無音stub**。`Sketch_9.2.1_Passive_Buzzer`が鳴らない |
| `Wire`(2) | 実装済(master)。16Bと14.1.xが該当 |

- [x] `Wire`(I2C)を実装した(`libraries/Wire/`、master専用、polling)。
      pinはvariantの`CH32_I2Cn_SCL/SDA`から。**X035実機で配線なしの自己検査11項目pass**
- [x] `SPI`を実装した(`libraries/SPI/`、controller専用、polling)。
      **X035実機で配線なしの自己検査9項目pass**(MISOをpull-upして0xFFが読めることを利用)
- [x] `interrupts()`/`noInterrupts()`を実装した。`api/Common.h`が
      「coreが定義すること」としていたのに**どこにも無かった**。`csrsi/csrci mstatus, 8`の1命令
- [x] `tone()`を実装した(「コアAPI」の同項目を参照)
- [x] `Servo`を実装した(移植ではなく実装。[ライブラリ](#ライブラリwire--spi--servo)参照)
- [ ] `[P2]` `LiquidCrystal`(4bit parallel)。純Arduino APIだけで書けるので同梱の判断だけ
- [ ] `[P2]` キット同梱libraryのzip 8種の可否は**目標に含めない**。参考までに、
      `IRremote` / `FlexiTimer2` / `NewPing`はAVRのtimer・portレジスタを直接触るため
      そのままでは動かない公算が大きい
- [ ] `[P2]` UNO pin番号との読み替え表。キットの配線図はD2〜D13 / A0〜A5前提で、
      CH32のpad名とは一対一に対応しない
- [ ] `[P2]` 5Vモジュールとの電圧整合(LCD1602、L293D、HC-SR04のECHO等)。
      CH32は3.3V。どのpadが5Vトレラントかはfamily・pinごとに違う

## 対応ペリフェラルの範囲

一覧は[ペリフェラル対応表](peripheral-support.ja.md)(peripheral × series)。
EVTの`EXAM/`ディレクトリからペリフェラルの有無を生成し、
こちらの実装状況を重ねた表で、`tools/generate/peripheral_matrix.py`が作ります。

**決定(2026-08-20、ユーザ指示)**

- EVTにある**基本ペリフェラルは対応したい**
- **USB PDは必ず載せる**(初回release対象)
- RTOS(FreeRTOS/RT-Thread/HarmonyOS/TencentOS)は**一覧には載せるが初回release対象外**
- USB host/deviceは**要判断**

- [x] ペリフェラル対応表を作った(2026-08-20)
- [ ] `[P1]` `[要判断]` 基本ペリフェラルのうち方針未決のもの:
      PWR(sleep)、IWDG/WWDG、RTC。
      いずれもArduinoに標準APIが無いか、libraryとして出すのが慣例
- [ ] `[P1]` `[要判断]` **`EEPROM`(flash emulation)を入れるか、入れるならページを予約するか**。
      消去単位はEVTを読んで分かった: **V003/X035は1KB、V20x/V307は4KB**
      (fast eraseは64B/256B)。device-dataは**flash容量しか持っていない**ので、
      入れるなら`CH32_I2C_HAS_RTR`と同じくFAMILY表へ定数として持つことになる(R-20のD-3)。
      判断が要るのは**linker scriptで末尾ページを予約するか**:
      - 予約する: 安全だが**全sketchがflashを失う**。V003では16KBのうち1KB=6%
      - 予約しない: 他コアにも例はあるが、大きいsketchがEEPROM領域を踏む
      - メニューにする: FQBNが増える
- [ ] `[P1]` **USB PDの実装方針**。対応siliconはV205 / L103 / M030 / X033・X035。
      Arduinoに前例が無いので、公開APIの形から決める必要がある
- [ ] `[P1]` `[要判断]` **USB device/hostのスタック選定**。調査は[R-22](research/usb-stack.ja.md)。
      要点:
      - WCHの既存物は全部独自API。**公式ArduinoコアはUSBスタックを持っていない**
      - **TinyUSB(MIT)にCH32の正式ドライバがある**(device FS/HS + **host FS**)。
        対応は**V103/V20x/V30x**のみで、**X033/X035はPR #3703が未マージ**、
        L103/M030/V205/V407/X3x5は未対応
      - WCHのhostファイルシステム層は`libRV3UFI.a`という**バイナリでしか無い**ので、
        「WCHのhostを使う」は方針以前にソースが無い
      - **本当の障害はクロック**。X035はHSIが48MHzなのでそのまま動くが、
        他は全部PLLが要る(下の`[P1]`)
- [ ] `[P2]` 上流貢献: TinyUSBのPR #3703(CH32X035)は**HILで試せていないこと**が
      マージの障害。**X035実機はこちらにある**ので、試して結果を返す価値が高い
- [ ] `[P2]` 表の空欄は「EVTに例が無い」であって「siliconに無い」ではない。
      実装時にreference manualか`ch32-device-data`で裏を取ること
      (V407のSysTick、V103のINTが実例)

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
- [x] **`tone()`を実装した**(`cores/arduino/wiring_tone.cpp`)。
      timerのupdate割込みでpinをtoggleする方式なのでpinを選ばない。
      使うtimerはvariantが選ぶ(`CH32_TONE_TIMER`)。
      空きが無いfamily(V003/X035/M030)では**PWMと共有**し、影響するpadを
      variantヘッダに列挙してある(AVRがpin 3/11で同じ制限を持つのと同じ扱い)。
      **C++で書いた**: `api/Common.h`が`tone`/`noTone`をC++プロトタイプ側にだけ
      置いているので、Cで定義するとリンクしない
- [ ] `[P1]` ADC分解能(`CH32_ADC_BITS`)はdatasheet由来。**実機で確認する** (要実機)
- [ ] `[P1]` `analogWrite`のPWM周波数が1kHz固定。Arduino慣例には合うが変更手段が無い
- [ ] `[P2]` ADC2以降を使えるようにする。現在ADC1のみ
- [ ] `[P2]` X305/X315のPWM。timerもper-pin AF方式でdefault routeが無い
- [x] `SPI`/`Wire`ライブラリ。Tier Aの要件([project-scope](project-scope.ja.md))。
      詳細と残作業は[ライブラリ(Wire / SPI)](#ライブラリwire--spi)へ

## ライブラリ(Wire / SPI / Servo)

どちらも`libraries/`に置いた同梱library。pinはvariantの生成マクロから来るので
`begin()`に引数は要らない。AFIO routeは**既定routeでも毎回書く**(`HardwareSerial`と同じ理由)。

- [x] `Wire`: master、polling、bufferは32バイト(`CH32_WIRE_BUFFER_SIZE`で変更可)。
      **待ちは全てtimeout付き**(`CH32_WIRE_TIMEOUT_US`、既定25ms)。
      pull-upが無い/デバイスが居ない、はI2Cの普通の失敗なので、
      そこで止まらず`endTransmission()`のエラーコードになる
- [x] `SPI`: controller、polling、NSSはsoftware(SSM/SSI)にしてpinを解放。
      `transfer16()`は8bit×2で送る(DFFを切り替えないので設定が半端にならない)
- [ ] `[P1]` **実デバイス相手の確認**(要実機・要配線)。今の自己検査は
      「バスに何も無くても固まらない」ことしか見ていない。
      I2CはEEPROMかLCD、SPIはMISO-MOSI短絡のloopbackが要る
- [ ] `[P1]` **slave / peripheral モード**。`Wire.begin(address)`・`onReceive`・`onRequest`と
      SPIのperipheralは**受け付けるが何もしない**。
      `SPI_HAS_PERIPHERAL_MODE`は意図的に未定義にしてある
- [ ] `[P1]` `Wire`の`setWireTimeout()`/`clearWireTimeout()`(AVR互換API)が無い
- [ ] `[P2]` `usingInterrupt()`が空実装。この実装はISRからバスを触らないので実害は無いが、
      sketch側がISRからSPIを使うと壊れる
- [ ] `[P2]` I2C/SPIとも**PCLK1 = F_CPU前提**。APB prescalerやPLLを入れたら追随が要る
- [ ] `[P2]` DMAを使っていない。長い転送はCPUを占有する

### Servo(2026-08-21実装)

- [x] `libraries/Servo/`。1本のtimerが最大12個のservoを順に駆動する方式(AVRと同じ)なので
      **pinを選ばない**。timerはvariantが選ぶ(`CH32_SERVO_TIMER`)
- [x] **tone()とは必ず別のtimer**にした。ブザーを鳴らしながらサーボを動かすのは普通の要求
- [x] timer選定の候補に**update割込みが独立ベクタを持つもの**(`TIM2_UP`等)も入れた。
      これで**全familyでtoneとServoが同時に成立**する。ただしV003/X035/M030では
      PWM用timerを食うので、その板の`analogWrite()`は影響を受ける(variantヘッダに列挙)
- [ ] `[P1]` 実機確認(要実機)。自己検査sketchは自分のpadのpulse幅を測っている
- [ ] `[P2]` TIM8〜TIM10(V30x/V4x7のAPB2側)を`ch32_registers.h`が持っていないので、
      timer候補から外している。持てば選択肢が広がる

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

- [x] **SDI print(debug moduleのmailbox経由の出力)を実機で受信できることを確認**
      (2026-08-20、X035 + WCH-LinkE fw 2.12)。UART配線もpinも消費せず、**coreはhaltしない**
      (`millis`が500ms刻みのまま進むことで確認)。
      - protocol: `DATA0`が0になるのを待ち、`DATA1`=byte3..6、`DATA0`=長さ|byte0..2<<8。
        7バイト/frame。番地`0xE0000380`/`0x384`は魔法の数ではなく、
        `hartinfo`が`dataaccess=1, datasize=2, dataaddr=0x380`と自己申告している
      - 有効化はprobeへのUSB command 1つ(`0x0d` payload `ee 00`、`ff`=非対応)。
        **probeはWCH-LinkE(CH32V305)限定、firmware 2.10以上**。手元は2.12
      - 対応chip(wlinkの判定): V003/V00x/V103/V20x/V30x/**X035**/L103/CH641/CH643/CH645。
        **V4x7/V205/M030/X3x5は対象外**
      - **受信はwlinkがsessionを保持している間だけ**。
        `wlink flash --enable-sdi-print --watch-serial`は通り、
        probe-rsで書いた後に`wlink sdi-print enable`だけしても届かない
        (attachでhaltし、`resume`は別sessionになるため)。wlinkが終了すると転送も止まる
      - vendorの`_write`は`DATA0`が0になるまで**無限に待つ**。debuggerを外すと固まるので、
        我々の実装では待ちを打ち切って捨てる(spikeで実装・確認済み)
- [x] **SDI printを独立したSerialクラスとして実装した**
      (`cores/arduino/SerialSDI.{h,cpp}`、instance名`SerialSDI`)。
      `_write`の`#if`で差し替える旧コア/WCH公式と違い、**UARTと同時に使える**。
      送信のみ。待ちは打ち切るので、hostが居なくても止まらない。
      TU を分けてあるので、使わないsketchはリンクされない。**実機確認は未**
      (spikeでは同じprotocolで受信できている)
- [ ] `[P1]` `[要判断]` SDI printの受信をどう提供するか。
      (a) wlinkを第2のuploaderとして同梱する、
      (b) 有効化commandは小さいので自前のtool/probe-rs patchで賄う、
      (c) 開発者向けに外部toolとして案内するだけ。
      なお[R-17](research/upload-programmers.ja.md)のとおりwlinkは**probeをserialで選べない**
      (`-d INDEX`のみ)ため、LinkEを複数挿す運用とは相性が悪い
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
- [x] **device-dataのsignal名正規化とremap fieldの再構築を取り込んだ**([R-19](research/signal-name-normalization.ja.md))。
      調査で分かったのは、名前より**fieldの定義のほうが重い**ということだった:
      `remap_fields.csv`は全selectorを単一registerとして持っていたが、
      L103/M103やV20x/V30xでは**PCFR1とPCFR2にまたがる**。コアはPCFR1しか
      書いていなかったので、route 2以上を使った瞬間に黙って別のrouteを選ぶ状態だった。
      上流が要件から作り直したテーブルを、EVTの`GPIO_PinRemapConfig`を
      ホストで実行して独立に検証([tools/generate/evt_remap_fields.py](../tools/generate/evt_remap_fields.py))、
      **235 selectorで完全一致**。`peripheral`/`role`列、既定route(value=0)、
      V407/V467の抽出漏れも解消されている。
      取り込みで`load_remap_fields`はregister修飾を要求するようになり
      (無ければ落とす)、variantは**fieldがまたぐregisterごとに**
      `CH32_SERIALn_REMAP{,2}_{MASK,VAL}`を持つ
- [ ] `[P1]` **上流へ報告: CH32V307に`SPI1_SCK/default`の行が無い**。
      `pin_functions.csv`はV307のSPI1についてMISO/MOSI/NSSのdefaultは持つのに
      SCKだけremap-1しか無く、そのため生成器がSPI1にremap-1(PB3/PB4/PB5)を選ぶ。
      結果として**SPI1(remap-1)とSPI3(default)が同じpadを名乗る**。
      実際のV307はPA5/PA6/PA7がSPI1の既定のはずなので、データ側の欠落
- [ ] `[P2]` 上流へ報告: `SPI3_MOSI（12）`のように**全角括弧つきのsignal名**がV307にある。
      現行の正規表現では拾わないので実害は無いが、R-19と同じ種類の揺れ
- [x] **決定(2026-08-21、ユーザ指示): `pins_arduino.h`の既定pinはデータシートどおり**、
      つまり既定routeのまま。coverage優先には切り替えない。
      X033/X035のI2C1のように**既定route(PA10/PA11)が7型番中2つにしかbondされていない**
      ケースは残るが、そこは「利用例では明示的にpinを選ぶ」で埋める
- [ ] `[P1]` **route定数を機械生成する**(2026-08-21、ユーザ指示)。
      `CH32V003_I2C1_0_SCL_PC2`のように**series・周辺・route番号・役割・pad**を名前に持つ定数を
      variantへ出す。狙いは**エディタ補完で選択肢が見えること**なので、
      値そのものより名前の並びが要件。
      **書式は未定**(`要判断`)で、生成は最後でよい。決めるときの論点:
      - series名を入れるか(1 variantに1 seriesなので冗長だが、補完の頭出しには効く)
      - route番号は`0`起点(=既定)でよいか、`default`/`remap-1`のどちらを書くか
      - padが型番によって無い場合の扱い(生成するが`digitalPinIsCommon`で弾けるようにする等)
      - 定数の型。`pins_arduino.h`のpad名(`PC2`)そのままにするか、専用の型で誤用を防ぐか
      - **`#define`ではなくnamespace階層にする案**(2026-08-21、ユーザ指示)。
        他コアの事例では`namespace variants_collector::esp32::esp32::<board>`の下に
        `struct Info`/`struct Pins`を置き、`static inline constexpr`で値を持たせている。
        **階層が見えるぶん補完が効く**のが利点で、名前空間の衝突も起きない。
        欠点は`#ifdef`で存在判定できないことと、C(`.c`)から使えないこと。
        **どれを採るかは決めない**: 自動生成なのだから何通りか作ってみて選ぶ
- [ ] `[P1]` `[要判断]` **レジスタマップを同梱するか**(2026-08-21、ユーザ指示で調査)。
      必要データの列挙は[R-20](research/register-map-data.ja.md)にD-1〜D-8として書いた。
      要点は3つ:
      - device-dataに**レジスタ関係の表は1つも無い**(`remap_fields.csv`が唯一の例外)
      - 既に[ch32-rs/ch32-data](https://github.com/ch32-rs/ch32-data)(MIT/Apache)がある。
        ただし**V205 / V407 / V467 / X305 / X315 / M030 / M103 / M007が見当たらない**
      - 粒度は「peripheral型 × 型version」。同じI2Cでも
        V003/X035にRTRが無くV20x/V30xにはある、という差が実在する
- [ ] `[P1]` **利用例(examples)は明示的にpinを選ぶ書き方にする**(2026-08-21、ユーザ指示)。
      既定routeがbondされていない型番があるので、`Wire.begin()`任せの例は
      「動かない板がある例」になってしまう。
      前提として`setRoute()`/`setPins()`相当がWire/SPI/Serialに要る
- [x] **既定route(value 0)でもfieldを書く**ようにした。`begin()`が事前の状態に
      依存しなくなる。初期化後に既定へ戻す・再初期化するのは通常操作であって
      例外ではないため。`uart_scan`では実際にこれが牙を剥き、
      前の候補のremapが残ったまま全routeが1つのpadから出ていた
- [x] **`setRoute(n)` / `setPins(...)`を実装した**(Serial / Wire / SPI)。
      DxCore型どおり両方を持ち、`bool`を返し、**別routeのpinを混ぜたら拒否**する
      (STM32duinoは`setRx`/`setTx`を独立に受けて衝突を見ない)。
      - variantが`CH32_<instance>_ROUTES`を生成する。1行が
        `{route番号, {pin×3}, PCFR1値, PCFR2値}`で、maskはinstance側が持つ(routeで変わらないため)
      - **型番でpadが変わるrouteは表に載せない**。1つのheaderがseries全体を担うので、
        パッケージ次第で別pinになるrouteは名前を付けられない
      - 表は`setRoute()`/`setPins()`からしか参照しないので、
        `--gc-sections`で**使わないsketchからは消える**
      - 開いている状態で呼ぶと、**古いpadをfloating inputへ戻してから**開き直す
        (USARTはbaudとframingを覚えていて同じ設定で再開する)
      - **X035実機で確認**: `setRoute(1)`でPA10/PA11へ移ると出力が消え、
        `setPins(PB10,PB11)`で戻ると再び出る。TX/RXを別routeから混ぜると`false`、
        存在しないroute番号も`false`
- [ ] `[P1]` PCFR2書き込み経路の**実機確認**。X035のSerialは既定routeなので踏めない。
      L103/M103/V203/V307のいずれかを載せたときに確認する
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

- [x] **LinkEのUART bridgeが固まる事象と復帰手順**(2026-08-21に遭遇)。
      SDI printの有効/無効を何度か切り替えたあと、
      **CDCが古いデータしか返さなくなった**(resetのたびに数十バイトだけ吐く)。
      route変更なしの素のsketchでも同じだったので、ファーム側ではなくprobeの状態。
      `probe-rs read`でflashを読むと**新しいimageは確かに書けていた**ので、
      書き込みではなくbridgeだけが壊れる。
      復帰は**USBの付け直し**。WSLでは`usbipd.exe detach --busid <id>`のあと
      `usbipd.exe attach --wsl --busid <id>`。
      なお`USBDEVFS_RESET`(ioctl)は**usbipのattachごと壊す**ので使わないこと
- [ ] `[P2]` 上の事象の切り分け。SDI printのenable/disableが原因かは未確定で、
      再現手順も未確立。HIL runnerに載せるなら「serialが無反応ならprobeを付け直す」を
      自動化する余地がある

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
- [x] `libraries/`(SPI/Wire)同梱後、**installした状態で`#include <SPI.h>`/`<Wire.h>`が
      解決されることを`install_check.py`で確認**(3本目のsketch`Libraries`)。
      release archiveのallowlistに`libraries`は元から入っていたが、
      入っていることと**解決されること**は別なので、compileで踏むようにした
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
