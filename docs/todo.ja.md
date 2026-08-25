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
      [tests/unit/test_clock_prescaler.py](../tests/unit/test_clock_prescaler.py)が
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
- [x] **`crt0_probe`を4 boardで実施**(2026-08-25、いずれも5 pass):
      CH32V103R8T6 / CH32V203C8T6 / CH32X035C8T6 / CH32L103C8T6。
      variantごとのlinker scriptとvector tableの動的検証がこれで4系統になった
- [ ] `[P2]` `crt0_probe`をCH32V307でも回す(要board接続)

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
- [x] **決定(2026-08-21、ユーザ指示): `EEPROM`は今はやらない**。
      調査は[R-26](research/eeprom.ja.md)。要点:
      - **X035にはword書き込みが無い**ので、全family共通の単位は
        fast page(64/128/256B)だけ。1バイト書くのにページ消去1回
      - 寿命はfamilyで桁違い。**X035/L103/V00xは保証300K**(ESP32の外付けSPI NORより多い)、
        **V003/V20x/V30xは保証10K**(typ 80Kは「保証しない」と明記)
      - **参照キットは50 sketch中0本**がEEPROMを使っていない。ESP32も非推奨化している
      - **実機実験で「通常の書き込みでは消えない領域が持てる」ことを確認**。
        probe-rsはimageが占めるsectorしか消さない(X035で1KB粒度)。
        `NOLOAD`で置けばESP32のNVS相当の性質がパーティションテーブル無しで得られる
      - 再開時は**パーティション方式**が候補。**ファイルシステムまでやるかは別途検討**
- [ ] `[P1]` **USB PDの実装方針**。対応siliconはV205 / L103 / M030 / X033・X035。
      Arduinoに前例が無いので、公開APIの形から決める必要がある
- [x] **決定(2026-08-21、ユーザ指示): USBは[TinyUSB](https://github.com/hathach/tinyusb)を採用**
      ([ADR-0012](adr/0012-usb-stack.ja.md))。自前スタックは書かない。
      **未対応seriesはforkせず上流へ載せにいく**のも方針。調査は[R-22](research/usb-stack.ja.md)
- [x] **決定(2026-08-21、ユーザ指示): ベンダヘッダ依存はshimで回避する**
      ([R-23](research/tinyusb-vendor-header.ja.md))。TinyUSBは無改造で使い、
      `variants/<SERIES>/ch32v20x.h`のような**同名ヘッダをこちらで生成**する。
      driverが要求するのは4つだけで、実際に無いのは**レジスタstructのみ**
      (IRQn・NVIC・SystemCoreClockは既存の生成物と`F_CPU`で足りる)
- [ ] `[P1]` shimを生成する。structは**EVTを読んで起こす**ことになるので、
      **EVTとoffsetを突き合わせる自動チェック**を必ず付ける(写し間違いを検出できる状態にする)
- [ ] `[P2]` **上流提案(自前struct)は「ずっと先」**。理由は保守の当て:
      手で写したstructを上流へ置けば人力追随の義務を負う。
      前提は[R-20](research/register-map-data.ja.md)、つまり
      **データが育ってレジスタ定義が自動反映される信頼**ができてから
- [ ] `[P1]` **上流貢献その2: PR #3703(X033/X035)を実機で検証して返す**。
      マージの障害は**HILで試せていないこと**。X035実機はこちらにある
- [ ] `[P1]` **上流貢献その3: L103/M103・M030・V205のUSBFS**。
      EVTヘッダのフィールド列を突き合わせた結果、**この3つはV20xと配置が一致**している
      (`UEPn_CTRL`別名が増えるだけ)。`tusb_mcu.h`のエントリ追加＋実機確認で載る見込み
- [ ] `[P2]` **上流貢献その4: V4x7配置のUSBHS**ドライバ。
      **V205 / V407・V467 / X305・X315の3グループは互いに完全一致**(101フィールド、
      `BASE_MODE`始まり)で、V307配置(127フィールド)とは別物。1つ書けば3 seriesに効く
- [x] **決定(2026-08-21、ユーザ指示): TinyUSBは内部に持つ(vendoring)**。
      「なにかパッチを入れる可能性もある」ため。
      前例は`cores/arduino/api`(ArduinoCore-API)で、版を固定して同梱し
      同期チェックを付ける形([tools/vendor/check_api_sync.py](../tools/vendor/check_api_sync.py))
- [x] **決定(2026-08-21、ユーザ指示): 最初に通すのはV203/V307**。両方実機がある。
      **上流サポート済みのfamilyで土台を固めてからX035へ行く**
      (X035は未マージPRが要るので、そこで躓くと切り分けが濁る)
- [ ] `[P1]` `[要判断]` **USB CDCを`Serial`にするか**(やるならFQBNメニューが要る)
- [x] **決定(2026-08-21、ユーザ指示): クロックはフル対応へ進める**。
      線引きは「**機構は全familyぶん一般化、既定値の引き上げは実機で検証できる範囲だけ**」。
      HSEとクロックメニューは今回入れない(HSEは**板の属性**でデータが無い)
- [x] **上流へのデータ整備依頼を先に用意した**([R-24](research/clock-data-request.ja.md))。
      依頼はC-1〜C-8。根拠として踏んだfamily差:
      - ツリーの段数が違う(V407は`SYSCLK≠HCLK`、X315は`SYSCLK/CoreCLK/HCLK`の3段)
      - **PLL制御がRCCの外にもある**(V20xの`EXTEN_CTR.PLL_HSI_PRE`)
      - **APB1が常に`/2`になる**(V20xのHSI PLL経路)。
        いまのUSART/I2C/SPIは`PCLK1 = F_CPU`前提なので**ここは必ず直す**
      - V20x/V30xのSDKは**flash latencyを触らない**。上げたとき何を設定すべきかのデータが無い
- [ ] `[P0]` **V203/V307のUSBには48MHzが要る = PLLが前提**。
      現在の[クロック方針](#クロック)は「HSI直結・PLLなし・boards.txt固定値」なので、
      **この決定を採るとクロック方針に手を入れることになる**。
      調べた範囲では**HSE無しでも経路はある**:
      - V20x: SDKに`SYSCLK_FREQ_48MHz_HSI`等の選択肢がある
      - V307(D8C): `RCC_HSBHSPLLCLKSource_HSI`があり、USBHS PLLの参照は`_8M`を選べる。
        USBFS側は`RCC_USBCLK48MCLKSource_USBPHY`でPHY PLLから取れる
      - ただし**WCHのUSB例はどれもHSE**を使っている。full speedの許容偏差は±0.25%で、
        HSIの確度がそこに収まるかは**実機で確かめるべき**(収まらなければHSE対応も要る)
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

## coreの範囲とexamples

**API監査で穴が5つ見つかった**(2026-08-21)。
examplesを書いて2つ、`core.a`のシンボルとArduinoの契約を突き合わせて3つ。

- [x] **`serialEvent()`が一度も呼ばれなかった**。`api/HardwareSerial.h`が
      `serialEventRun()`をweakで宣言しているのに、`main()`が呼んでいなかった。
      **参照キットは50 sketch中3本がserialEvent()を使っている**ので直撃だった。
      `HardwareSerial.cpp`に`arduino::serialEventRun()`を実装し、
      `serialEvent()`(monitor port)と`serialEvent1..5()`を配る。
      定義をSerialと同じTUに置いたので、**Serialを使わないsketchはリンクしない**
      (AVRコアと同じ構造)。**X035実機で動作確認済み**
- [x] **`yield()`がweakでなかった**。自前の`yield()`を定義したsketchが
      リンクエラーになる。`delay()`が呼ぶ以上、差し替えられることが契約
- [x] **`initVariant()`を`main()`が呼んでいなかった**。APIが宣言しているフック。
      weakの空実装を用意して呼ぶようにした。
      **`init()`は敢えて呼ばない**: AVRではあれは*コア自身*のハードウェア初期化
      (timerやADCの設定)であってユーザフックではなく、
      こちらは`SystemInit`とcrt0がmainより前に済ませている。
      空の`init()`を呼ぶのは全sketchが払う儀式にしかならない
- [x] **サイズ基準線を更新した**。上の変更でBlinkの`.text`が**全部品で+48バイト**
      (V003で812→860)。内訳は`initVariant()`の呼び出しと、
      loopごとの`serialEventRun`のnullチェック。
      **AVRコアも同じ構造で同じコストを払っている**。V003の16KBに対して0.3%

**ESP32互換のためのAPI追加**(2026-08-21、ユーザ指示「ESP32との互換性のほうが嬉しい」):

- [x] `digitalPinToPort()` / `digitalPinToBitMask()` /
      `portOutputRegister()` / `portInputRegister()`を追加。
      **ESP32と同じ形**(`volatile uint32_t*`で、1ビット=1ピン)。
      CH32の`OUTDR`/`INDR`は意味論が一致し、X033/X035の24ビットポートも1本に収まる
- [x] **`portModeRegister()`は敢えて提供しない**。CH32のモードは1ビットではなく
      `CFGLR`/`CFGHR`/`CFGXR`に散る4ビットのフィールドで、1本のポインタでは表せない。
      `CFGLR`を返せばbit 8以上で黙って壊れるので、**コンパイルが通らない方を選ぶ**
- [x] ポートのbase addressは`ch32_pins.h`に置いた(Arduino.hが
      `ch32_registers.h`を読まずに済ませるため)。二重定義になるので
      `wiring_digital.c`で`_Static_assert`により一致を強制している
- [x] **`Serial.availableForWrite()`を実装**。`Print`の既定は常に0で、
      「送信バッファが永久に満杯」に見えていた。
      todoの「`write()`が満杯でblockする/non-blockingにする手段が無い」への答えでもある
- [x] `core_api`自己検査に5項目追加(**X035実機で全項目pass**)。
      なおAVR固有の`_BV()`等は追わない(ユーザ判断: キットのAVR前提ライブラリは対象外)

**examplesを書いて見つかったぶん**:

- [x] `analogReadResolution()` / `analogWriteResolution()`は**実装済みなのに宣言が無く**、
      sketchから呼べなかった。ArduinoCore-APIのこの版が宣言していないため。
      `Arduino.h`の`extern "C"`ブロックへ追加した
- [x] `dtostrf()`は**ヘッダを同梱しているのにinclude pathに無かった**ので
      `<avr/dtostrf.h>`が解決できなかった。
      `api/deprecated-avr-comp`をinclude pathへ追加(samd/renesasと同じ)。
      これで`<avr/pgmspace.h>`も届く

範囲の判断基準・examplesの規約・レジスタ公開の懸念は[R-25](research/core-scope.ja.md)。

- [x] **`SerialSDI`をcoreから`libraries/`へ移した**(2026-08-21)。
      ArduinoCore-APIが宣言しておらず、coreの他ファイルからの参照も無かったため。
      `#include <SerialSDI.h>`の綴りは変わらないのでsketchへの影響は無い
- [x] **`printf`の出力先を差し替えられるようにした**(`ch32_set_stdout(Print*)`)。
      既定はmonitor port。**stdioだけ**動き、`Serial`という名前は追随しない。
      USB CDCが来ても同じ口を使う
- [x] **examplesの置き場所と規約を決めた**。ライブラリ機能はそのライブラリの`examples/`、
      coreのAPIは`libraries/CH32/examples/`。
      Arduinoは**ライブラリ経由でしかexamplesを配れない**ためで、
      ESP32も同じ理由で`libraries/ESP32/`に`src/dummy.h`を置いている。
      こちらは同じファイルに「レジスタの逃げ道」という仕事を与えた(`CH32.h`)
- [x] **全examplesをCIでコンパイル**する([tests/compile/test_examples.py](../tests/compile/test_examples.py))。
      X035とV003の2 board。現在11 example
- [x] 各ライブラリに`keywords.txt`を置いた
- [x] **決定(2026-08-21、ユーザ指示): 同梱ライブラリの方針を承認**
      → [ADR-0013](adr/0013-bundled-libraries.ja.md)。
      coreの3条件・同梱の3基準・examplesの置き場所を1本にまとめた
- [x] **examplesを19本に増やし、core APIをひととおり網羅した**。
      `libraries/CH32/`に14本(Blink / SerialEcho / AnalogRead / Fade / ToneMelody /
      PinInterrupt / ShiftOut / PulseIn / Timing / RandomNumbers /
      AnalogResolution / CriticalSection / PrintFormatting / PinCapabilities)、
      各ライブラリに5本
- [x] **全ライブラリに`README.md`と`README.ja.md`を置いた**。
      使い方だけでなく**注意事項**(プルアップは自前・timeoutの意味・
      slave未実装・timer共有・サーボの電源・SDIはhost側の準備が要る、等)を書いた。
      `tests/compile/test_examples.py`がREADME 2本と`keywords.txt`の存在を検査する
- [ ] `[P2]` examplesをさらに増やす。実デバイスを使う例(EEPROM/LCD/センサ)は、
      外部ライブラリに依存しない範囲でどこまでやるかを決めてから

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
- [x] **Wireのslave modeを実装した**(2026-08-25、**配線ありの実機検証は未**)。
      割込み駆動(`I2Cn_EV`/`I2Cn_ER`、instanceがIRQ番号を持つ)。
      AVRの意味論を維持: `onReceive(count)`はSTOP後に割込み文脈で、
      `onRequest()`はaddress match時に(ADDRクリア前なのでSCL stretchで
      masterは待たされる)、over-readは**0xFF**(直前byteの繰り返しではなく)、
      バッファ超過は頭を残して尾を捨てる。master/slaveは排他
      (`begin()`/`begin(addr)`で選び、slave中のmaster呼び出しは4/0を返す)。
      検証の現状:
      - **配線なし**: `wire_selftest`に3 check追加(slave成立・勝手に喋らない・
        masterへ戻れる)。**CH32V103実機で14/14 pass**。V003/X035/V103/X315
        (I2C2のみ構成含む)でcompile確認
      - **配線あり**: [`manual/i2c_loopback`](../tests/manual/i2c_loopback/i2c_loopback.py)を
        用意(I2C1 master↔I2C2 slave、PB6-PB10 / PB7-PB11 + pull-up 2.2k〜10k)。
        データ経路・callback・0xFF filler・バッファ上限の10 check。
        **ジャンパ2本とpull-up 2本の配線待ち**(V103/V203/L103で可)
- [ ] `[P1]` SPIのperipheral(slave)モードは未実装のまま。
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
- [ ] `[P1]` **`Serial.print(float)`はCH32V003で約9.4 KB**(2026-08-22実測)。
      `menu.printf`とは別の話で、こちらはC++の`Print`側。
      `Print::printFloat`が`double`を取り(ArduinoCore-API由来、ADR-0009で無改変)、
      rv32ecにFPUが無いのでsoft-float一式が丸ごと入る。

      | symbol | bytes |
      |---|---|
      | `__adddf3` | 2346 |
      | `__subdf3` | 2252 |
      | `__divdf3` | 1818 |
      | `__muldf3` | 1510 |
      | `Print::printFloat` | 468 |
      | `__clz_tab` / `__ltdf2` / `__gtdf2` ほか | 約1000 |

      `core_api`が`Serial.println(1.5, 2)`の**1行**で15972バイト(16 KBの97%)、
      外すと6544バイト(39%)になる。**この1行を`print_format` caseへ分離した**
      (2026-08-22)——分割後は`core_api` 6464バイト(39%)、
      `print_format` 12772バイト(77%)で、`sync_profiles.py`が両方へ同じboard一覧を
      配るのでカバレッジは減っていない。期待値は`cores/arduino/api/Print.cpp`を
      読んで導いたもの(`1.50` / `3.1416` / `2.00` / `3`)で、
      **CH32V103実機で4つとも一致**。
      **文書化のみで対応する**(2026-08-22判断)。
      [docs/flash-size.ja.md](flash-size.ja.md)に、何が高いか・map fileの読み方・
      削り方をまとめた。`float`版`printFloat`の追加は**やらない**——
      ArduinoCore-APIの署名を変えることになり、しかも`print(1.5, 2)`の`1.5`は
      `double`リテラルなので一番ありがちな書き方が救われない。
      やるなら上流のArduinoCore-APIへ
- [ ] `[P2]` `[要判断]` **CH32V003で高コスト関数の呼び出しをビルドエラーにするboard設定**。
      `-Wl,--wrap`や`--defsym`で`__adddf3`等を弾けば「知らずに9.4 KB持っていかれる」
      事故は防げる。**2026-08-22時点では入れない判断**——逃げ道の設計
      (意図的に使いたいときにどう外すか)が要るため。
      [docs/flash-size.ja.md](flash-size.ja.md)の「検討したが入れていないもの」に記録済み
- [ ] `[P1]` `menu.printf`の文言をdocumentへ。ADR-0004が求める
      「nanoの`%f`非対応はArduino利用者の既知の落とし穴」の明示がまだREADMEに無い
- [ ] `[P1]` `__stack_size`の既定が512バイト。`printf`は簡単に超える。
      variantかmenuで変えられるようにする。現状は`_sbrk`が`_heap_end`で止めるだけで、
      stack自体のoverflow検出は無い
- [ ] `[P2]` heapの断片化と`realloc`の実機確認。`heap_string`はまだ素直な経路しか見ていない
- [x] `_fstat`が`st_blksize`を設定するようにした(64)。newlibの`__swhatbuf_r`が読むので、
      未設定だとstdoutのbuffer sizeがstack上のゴミで決まっていた

## USB PD (X035)

**方針(2026-08-25)**: **PDを先にやる。USB deviceは後回し**(利用者判断——deviceは
classの種類が多く、それぞれ実機確認まで要るので範囲が大きい)。

- [x] **下調べ完了**(2026-08-25)。参照実装3系統を確認した。**いずれも参照のみ**:

      | 実装 | 規模 | 内容 | ライセンス |
      |---|---|---|---|
      | WCH EVT `USBPD_SNK` | `PD_Process.c` 824行 | sink | EVT(参照のみ) |
      | WCH EVT `USBPD_SRC` | `PD_Process.c` 853行 | source | 同上 |
      | WCH EVT `USBPD_CH211` | `PD_Prot.c` 584 + `PD_VDM.c` 528 | protocol層 + VDM(alt-mode) | 同上 |
      | [wagiminator PD Adapter](https://github.com/wagiminator/CH32X035-USB-PD-Adapter) | `usbpd_sink.c` 488行 | sink + PPS | **CC BY-SA 3.0 = share-alike**。コードは持ち込めない。**価値はAPIの形** |

- [x] **X035のPHYはハードウェアでBMCをやる**。ビットバンではないので、
      仕事は「protocolとstate machine」であって信号生成ではない。
      レジスタのbase addressは**上流にある**(`memory_map.csv`の
      `CH32X035,USBPD,0x40027000`、155c398以降)
- [x] **クロックは既に条件を満たしている**。X035は`build.f_cpu=48000000L`
      (HSI直結、**PLL不要**)で、参照実装のBMCタイミング定数も48MHzが第一候補
      (`USBPD_TMR_TX=80-1` / `RX=120-1`)。
      **つまりPDは`[P0]`のPLL/クロック方針に触らない**——X035を先にやる判断の根拠
- [x] **範囲が決まった**(2026-08-25)。
      **主目的は「充電器のプロファイル取得」と「PPS」**、つまり
      Source Capabilitiesの列挙と`setVoltage(mV)`。
      **sourceも対応したいが優先度は低い**(後追い)。VDM/alt-modeは対象外
- [x] **同梱library**にする([ADR-0013](adr/0013-bundled-libraries.ja.md))。
      **X035専用ではない**——当初そう書いたのは誤り。
- [x] **対象は7 series。placementは2種類**(155c398の`memory_map.csv`):

      | base | series |
      |---|---|
      | `0x40027000` | **CH32L103** / CH32V205 / **CH32X035** |
      | `0x40024400` | CH32H417 / CH32X315 |

      CCのpadも**series毎に違う**(`pin_roles.csv`の`peripheral=USBPD`):
      X035 `PC14`/`PC15`、**L103 `PB6`/`PB7`**、V205 `PA0`/`PA1`、
      X305 `PD4`/`PD5`、H41x `PB3`/`PB4`、**M030は`CC1`〜`CC4`の4本**。
      よってlibraryは変異体の`CH32_USBPD_BASE` / `CH32_USBPD_CC1_PIN`…で
      出し分ける形になる——Serial/I2C/SPI/toneと同じ作り。
      **7 seriesとも既にboard定義がある**。うち**L103とX035は実機が繋がっている**
- [ ] `[P0]` **生成に要る表が`155c398`側にしか無い**。
      `memory_map.csv`(base) も `pin_roles.csv`(CCのpad) も `pin_alternate.csv` も
      **`b1285de`には存在しない**。
      つまり**変異体のdefineを生成する工程は取り込み後**になる。
      それまでは手書きのdefineで先に進める(検証用、承認とは別管理)
- [ ] `[P1]` **APIの形**。参照実装(wagiminator)は15関数で、形としては素直:
      `connect` / `negotiate` / `setVoltage(mV)` / PDOの列挙(`getPDONum`,
      `getFixedNum`, `getPPSNum`, `getPDOVoltage(n)`, `getPDOMaxCurrent(n)`) /
      現在値(`getPDO`, `getVoltage`, `getCurrent`)。
      Arduino風に直すならclass1つ。**Arduino標準APIには前例が無い**ので新規設計
- [ ] `[P1]` 実装の中身: CC1/CC2の接続検出 → Good CRCとmessage ID → sinkの
      state machine(参照実装は10状態: `IDLE`→`CHECK_CONNECT`→`CONNECT`→
      `SOURCE_CAP`→`SEND_REQUEST`→`WAIT_ACCEPT`→`ACCEPT`→`WAIT_PS_RDY`→
      `PS_RDY`、加えて`GET_SOURCE_CAP`) → PDO解析(Fixed / PPS) → Request
- [x] **ロジック層とAPIの形を実装した**(2026-08-25、**未承認・検証済み**、A-8)。
      [`libraries/USBPD/`](../libraries/USBPD/README.ja.md)。
      - `pd_frames.c/h`: **レジスタもArduino.hも知らない純関数**。
        Source Capabilitiesの解析(Fixed/PPS/Battery/Variable/AVS)、
        プロファイル選択(`pd_pick`)、Requestの組み立て(Fixed RDO / PPS RDO)、
        messageヘッダ。単位換算(10mA/50mA/50mV/100mV/20mV/250mW)はこの中だけ
      - 検証は**同じベクタを2回**: hostのccで共有ライブラリにして
        `tests/unit/test_pd_frames.py`(ctypes、14 test)、
        実機で`tests/sketches/basic/pd_selftest`(18 check、
        **CH32V103実機でfailures=0**)。V003(16K、31%)とX035にもcompileが通る
      - API: `USBPD.begin()/ready()/profileCount()/profile(i)/request(mV,mA)/
        requestProfile(i,mV,mA)/voltage()/current()/maintain()`。
        全部mV/mA。設計判断: **固定を優先**(PPS契約は数秒毎の再要求が要り、
        `delay()`中に死ぬ)、**中間電圧を丸めない**(`request(8000)`は
        5/9/12V充電器では失敗)、battery/variableは列挙のみ、
        `maintain()`は`Ethernet.maintain()`と同じ役どころ
      - `begin()`は**正直にfalse**(Wire slaveと同じ規則)。
        ハードウェアドライバが次の段
- [ ] `[P1]` `[要判断]` **ポンプ関数の名前**(いま`maintain()`)。
      loop()から呼び、PPSの再要求と、将来は再広告への応答・割込みの繰り越し
      仕事を担う。候補と論点(2026-08-25の議論):
      - `poll()` — BLE.poll()と同構造の公式前例
      - `update()` — 最も通じる。**「loopで呼ぶ動詞」としての読みは確立している**
        (maintainerの指摘。Bounce2等は毎loop呼びが普通で、一度きりには読まれない。
        当初の「一度きりに読める」という反対根拠は弱い)。残る差は
        「何をするか名前が言わない」ことだけ
      - `task()` — 同梱TinyUSB(tud_task)と揃う
      - `maintain()` — Ethernet.maintain()と意味一致だが、前例を知らないと不明瞭
      **未決のまま保留**(maintainer「もう少し考える」)。リリース前に確定させる。
      renameは機械的(USBPD.h/cpp、README×2、keywords、example、docs)
- [x] **ハードウェアドライバを実装した**(2026-08-25、**未承認・部分検証**、A-9)。
      CC検出(comparator 0.22V/0.66V + Rd)・BMC送受信(DMA + BMC_CLK_CNT)・
      GoodCRC(ソフトで返す。X035のPHYはCRC32とBMC符号化までが機械)・
      sink state machine(DETACHED→WAIT_CAPS→WAIT_ACCEPT→WAIT_PS_RDY→READY)。
      定義は`usbpd_hw.h`に**手書き(X035/X033のみ、取り込み後に生成へ置換)**。
      実機(CH32X035)で: begin()成立 / 300msのattach polling後に**空きポートを
      未接続と報告** / capsなしのrequest拒否 / end→beginが回る——
      `pd_selftest` 23 check pass(V003等はSKIP分岐 + 「begin()がfalseを正直に
      返す」check)。**negotiationはPD電源が来るまで未検証**。
      割り切り(ヘッダに明記): 接続時に自動でprofile 0(5V)をrequest(仕様上の義務)、
      再送なし、取り外し非検出(VBUS監視が要る)、無関係メッセージは無視
- [ ] `[P1]` `[要機材]` PD電源が来たら: attach→自動5V→profile列挙→`request(9000)`→
      PPS(`requestProfile`)→`maintain()`のkeepaliveの順に実機確認。
      期待手順は`manual/`に起こしてから(いまはsketches/basic/pd_selftestのhw節のみ)
- [ ] `[P1]` `[要機材]` **PD電源が来たら**: negotiationの実機確認。
      `setVoltage`まで見るなら**PPS対応**が要る。
      **電圧測定は初回は無し**(利用者判断)——`getVoltage()`が返す
      「交渉した結果」までを確認し、実際に何Vが出ているかの計測は後回し。
      **CC配線は済んでいる**(2026-08-25 maintainer: このbenchのboardで
      PDOプロファイルを確認したことがある)。PD電源の接続だけが残り
- [ ] `[P2]` USB device(TinyUSB)は後回し。再開するときの状態:
      TinyUSB 0.21.0はvendor済みだが**glueが1つも無い**
      (`tusb_config.h` / `TinyUSB.h` / `TinyUSB.cpp` / `ch32_tusb_glue.cpp`は
      lockに名前があるだけで**未作成**)。
      上流に**X035のDCDが無い**(`OPT_MCU_CH32*`はV307/F20x/V20x/V103/CH583)が、
      ドライバのコメントが「newer USBFS IP (CH32V20x/V307/**X035**)」と書いているので
      **移植であって新規実装ではない**。
      なおPD用のTCDは`portable/wch/`に**1本も無い**ので、`typec/usbc.c`の下は空

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
      (`libraries/SerialSDI/`、instance名`SerialSDI`)。
      **2026-08-21にcoreからライブラリへ移した**([R-25](research/core-scope.ja.md))。
      `#include <SerialSDI.h>`という綴りは変わらないのでsketchへの影響は無い。
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
- [ ] `[P1]` `[要判断]` **使わない`SerialN`が1本あたり192バイトのRAMを取る**
      (2026-08-25実測)。`HardwareSerial.cpp`が`Serial1`〜`SerialN`を1つの
      translation unitで定義しているため、vector table → `USARTn_IRQHandler`
      → `SerialN`という参照の鎖ができて`--gc-sections`が落とせない。

      | board | 定義される本数 | `.bss`のうちSerial | sketchが使うのは1本 |
      |---|---:|---|---|
      | CH32L103 | 4 | **768 / 1360 B (56%)** | 576 Bが死蔵 |
      | CH32V307 | 5 | 960 B | 768 Bが死蔵 |

      (`serial_println`をCH32L103:ANYでビルドし、map fileの`.bss.SerialN`が
      各`0xc0`であることを確認。RX/TX ring bufferが64 Bずつ)

      **直し方はもう分かっている**: vector tableのhandlerは既にweak alias
      (`crt0_ch32.S`の`Default_Handler`)なので、`SerialN`とそのISRを
      **1本ずつ別のtranslation unitへ分ければ**、参照されないarchive memberは
      そもそも取り込まれず、vectorはweak defaultのままになる。
      `libraries/SerialSDI`が既に同じ手を使っている。
      構造変更なので判断が要る。CH32V003(RAM 2K)はUSARTが1本なので影響なし
- [ ] `[P2]` **`SerialN.setPins()` / `setRoute()`は既に全portにある**——`Serial`
      専用ではない(`HardwareSerial.h`)。ただし受け付けるのは**生成されたroute表に
      ある組み合わせだけ**で、任意のpinは`false`を返す。
      AFIOのremapは「このUSARTはこの組」という単位でしか動かないので設計としては
      正しいが、**利用者から見て「pinを指定できる」のか「routeを選べる」のかが
      曖昧**。呼び名とドキュメントを決める
- [ ] `[P1]` `[要判断]` **USART1が使えないboardの`Serial`をどうするか**。
      series単位では生成器が既に解決していて、`CH32M103` / `CH32X033` /
      `CH32X315`は`CH32_SERIAL_DEFAULT`が**USART2**になっている。
      残る2つが未解決:
      - **型番単位**。既定routeのpadが小さいpackageで出ていない場合がある
        (L103は6型番中5型番)。`ANY`で焼くとその型番だけSerialが無音になる
      - **利用者の上書き**。いまは`-DCH32_SERIAL_DEFAULT=2`をbuild propertyで
        渡すしかない。boards.txtのmenuにするか、`sketch`側のAPIにするか

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

- [ ] `[P1]` **上流`c2c457d`は取り込まない。上流の修正が終わってから**
      (2026-08-22判断、pinは`b1285de`のまま)。
      F-2(CH32V20xの`AFIO_PCFR2_`)は解決しているが、同じcommitで
      **pin表のsignal名が2つ連結された行**が入っている。
      104行 / 14 part number / 4 series(V208 84、M030 10、V407 5、V467 5)。
      `remap_routes.csv`にも同じ形があり、そちらは**route番号やselector値まで
      巻き込む**(`SCL_5T1ET` = `SCL`(remap-5) + `T1ET`、`role`列も`SDA_2SPI_NSS`に壊れる)。

      | 例 | = |
      |---|---|
      | `ADC_IN6TIM3_CH1` (V208 PA6) | `ADC_IN6` + `TIM3_CH1` |
      | `USART3_TXOPA2_CH0N` (V208 PB10) | `USART3_TX` + `OPA2_CH0N` |
      | `SPI3_MOSII2S3_SD` (V407 PB5) | `SPI3_MOSI` + `I2S3_SD` |
      | `I3C_SCL_1SPI1_MISO` (V407 PB4) | `I3C_SCL`(remap-1) + `SPI1_MISO` |
      | `I2C_SDA_2SPI_NSS` (V007 PC4, remap_routes) | `I2C_SDA`(val=2) + `SPI_NSS` |

      検出は機械的にできる: **新表にしか無いsignal名のうち、旧表が知っている
      2つの名前へ綺麗に割れるもの**。86件中28件が該当した。
      同じcommitでF-4系の切れは直っている(`MC`+`O`→`MCO`、`USART1_RT`→`USART1_RTS`、
      `LTDC_V`+`SYNC`→`LTDC_VSYNC`、M030 PB2/PB3の`N`が消えた)ので、
      **run-onだけが問題**。
      原因不明が2件残る: M030 PB2/PB3の`TIM3_CH1`/`TIM3_CH2`(default)が
      run-onの形でなく消失、V205 PB1のPWMがどの表も無変更なのに消える。
      **上流で全面調査中**(2026-08-22)。報告内容は
      `scratchpad/runon_report.md`に出した形で渡してある。
      我々の生成物への影響は7 variant:
      V407/V467のSPI3既定routeがPB3/PB4/PB5→PC10/PC11/PC12、
      V208のADC A0〜A4・A6・A7消失(16→9)とUSART3既定のremap-1化、
      V205のPWM全滅(`analogWrite()`が効かなくなる)、M030のPWM pad PB2/PB3消失、
      V007のI2C1 route 2消失、X033のI2C1 route 5消失。
      新しい警告も1件: `CH32V003: clock_init step(s) not emitted: step 6 (trim)`
- [x] **上流`155c398`で検証(2026-08-25)。run-onは直っている**(28件→0件。検出器が
      拾う4件は`ISOURCE1` / `VDDIO` / `ETH_RMII_PPS_OUT`等の**誤検出**)。
      切れも解消(`MC`→`MCO`、`DD`→`VDD`、脚注記号の除去。211名前が消えた)。
      **原因不明だった2件も解決していて、しかも正しい**: M030 PB2/PB3も
      V205 PB1も**英語表と中国語表の食い違い**で、上流が中国語表(`TIM3_CH1N` /
      `TIM1_CH3N`)を採った。どちらも相補出力なので`analogWrite()`の対象外——
      **以前のPWM padのほうが誤読由来だった**。
      基準線も確認済み: `--tables <b1285de> --check`はexit 0 / DRIFT無しなので、
      下記の差分は純粋に上流の差。全文は[実験0015](experiments/0015-device-data-155c398-adoption.ja.md)
- [ ] `[P0]` **(要判断)** `155c398`を取り込むか。生成物は**8 additive / 15 rewriting**。
      c2c457dで壊れていた4件(V407/V467 SPI3、V208 ADC、V007 I2C1、X033 I2C1)は
      **すべて直っている**。増えるもの: PC13/PC14/PC15が7 boardに追加、
      linker script 8本、V208 USART4 route表。

      **2026-08-25に上流が(a)(b)(d)へ回答。3件とも「上流が正しい」で決着。**

      - **(a) flashが6 boardで縮む → 256K等が正しい。** 根拠3つ:
        `memory_configs.csv`がV307VCT6の5通りを持ち`datasheet_value=1`が
        256K+64K(素の構成)を指す / 480Kは`code_flash_bytes`で、V317の注記が
        「480Kは領域全体、設定した分が零等待、残りが非零等待」と言っている /
        同梱`Link.ld`がV303CB〜RB=128K、V307VC/WC/RC・V303VC/RC=288K(最大設定)。
        **480Kは「使えるflashサイズ」ではなかった**
      - **(b) V303/V305/V307のSPI1既定route変更は「意見の変更」ではなく「復元」。**
        `afio-spi1-remap`が`reset_value=0` / `valid=0;1`で、
        PA4/PA5/PA6/PA7がdefault、PA15/PB3/PB4/PB5がremap-1。
        以前はremap-1しか無く生成器に選択肢が無かっただけ
      - **(d) PC13/PC14/PC15のI2C2 af-7は列ずれではない**(下記で決着)

      残る判断は**(c) V205からI2C2が消える**だけで、これは我々の生成器の問題(下記)
- [ ] `[P1]` **`(a)`の副産物: flash分割をmenuで出せる**。
      `memory_configs.csv`がV307VCT6で5通りの構成を持っている。
      いまは素の構成(256K+64K)だけを`upload.maximum_size`にしているが、
      零等待/非零等待の分割は利用者が選べる性質のもの。**要判断**
- [ ] `[P1]` **生成器: 同じ`(instance, route, role)`に2つのpadが来ると黙って後勝ち**。
      `load_pin_routes()`が`out[part][(index,route)][role] = pad`で上書きするため、
      どちらが採られたか出力から分からない。**`b1285de`でも既に起きている**
      (`CH32V203CCT6`のI2C1 af-7はPA14/PB6/PB8の3候補があり黙ってPB8)。
      `155c398`ではI2C2 af-7がPB10/PB11とPC13/PC14の2組になり、V205で表面化した。
      **当初「衝突として落とすべき」と書いたが、上流の調査で誤りと分かった**
      (2026-08-25)。`(型番, 周辺, 役割, 経路)`に複数padがある組は
      **984 / 22453 (4.4%)**あり、内訳は**af-N 860 / default 101 / remap-N 23**。
      **AF方式では衝突ではなく選択肢**——padごとに独立してAF番号を選ぶので、
      同じ機能を出せるpadが並ぶのが正常。排他なのはremap方式(1つのfield値が
      pad一式を切り替える)だけ。
      要るのは**落とすことではなく、どれを選んだかを出力に残すこと**と、
      選び方を決定的にすること。remap-Nの重複だけはエラーにしてよい。

      **前半(可視化)は実装した**(2026-08-25、**未承認・検証済み**)。
      `load_pin_routes()`が候補を全部拾い、生成器が最後に要約を出し、
      variantのheaderに採らなかったpadをコメントで書く。挙動は不変
      (`--check`はコメント追加のみ、6 variant)。`b1285de`での実測は
      **193組(af-N 183 / default 10 / remap-N 0)**。
      remap-Nが0なのは、上流が見つけた23組が我々の読まない表にあるため。

      ```
      /* USART1: route af-1, on every part */
      #define CH32_SERIAL1_TX PC4
      /* device-data lists PA11, PC4 for TX on this
       * route, in that order, and the last is the one above. */
      ```

      **後半(選び方)は要判断。まず素朴な規則が危険だと分かった。**
      「対応型番が多いpad、同数なら若い番号」を試算すると**35箇所が動き**、
      そのうち候補は全部同数なので実質「若い番号」になり、
      **V205のI2C1がPA13/PA14、X305/X315のUSART3もPA13/PA14**——
      **どちらもSWD pin**を既定に選んでしまう。踏むとdebug接続が切れる。
      規則を入れるなら**SWD padの除外が先**
      (`gpio_loopback`のREADMEが既に「絶対に使うな」と書いている pad)。
      なお`CH32V303/V305/V307`の`spi3 default MOSI`が`PB5`と`PA15`の2候補
      (default経路の10組の1つ)なのも、この試算で見つかった
- [x] **`CH32V203CCT6`のPC13/PC14/PC15のI2C2 af-7は正しい**(2026-08-25、上流回答)。
      列ずれを疑ったが外れ——AF番号は**signal名と同じセルの中に括弧で**書かれて
      いるので、列がずれれば名前ごとずれる。しかもp17とp25の**独立した2つの表**に
      同じ内容で出る。`PC13-TAMPER-RTC`は`TAMPER/RTC/TIM1_CH4(AF0)/I2C2_SCL(AF7)`
- [ ] `[P2]` **上流のF-27 / F-28を待つ必要はない**(2026-08-25に影響を確認)。
      上流が`remap-N`の23組を追ったところ3群が別方向で、うち2件が上流側の問題:
      **F-27** CH32V103 TIM3のremap値が誤り(RM表10-12は00/10/11でPB4=2・PC6=3、
      pin表が`TIM3_CH1_1`を両方に書いている)、
      **F-28** CH32L103のremap格子が`extract_remap.extract()`で0行
      (表10-17〜10-20はp84にある)。L103のremap経路はpin表だけが根拠。

      **我々の生成物への影響は今のところ無い**:
      - F-27: **タイマのremapは1箇所も使っていない**。`load_pwm_pins()`は
        `default`/`main`しか読まず、`load_remap_fields()`はUSART/I2C/SPIだけ。
        `variants/`にも`TIM._REMAP`は無い
      - F-28: **L103の既定は全て`REMAP_VAL 0`**(reset既定)なので、出荷経路は
        格子に依存しない。依存するのは`CH32_SERIAL1_ROUTES`のroute 1
        (PB6/PB7、PCFR1値`0x4`)だけで、これは`setRoute(1)`からしか触れない。
        `route_selftest`が実機でそこを通って戻ってきているが、
        **PB6/PB7には何も繋がっていないのでpinが実際に動いたかは未確認**。
        確かめるならprobeをPB6/PB7へ配線し直して`uart_scan`

- [ ] [P2]` **`systick.csv`が入ったのでCH32V103のSysTick配置をデータ由来にできる**。
      いま`cores/arduino/ch32_registers.h`に手書きしてあるoffsetと、
      「カウンタはbyte writeのみ」という制約(`CH32_SYSTICK_WRITE8`)は、
      上流表の`offset`と`write_bits`にそのまま載っている。
      54行、`basis=evt(core_riscv.h)`。取り込み判断の後に着手する

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

- [x] **(実機)** LinkEを4台分確認(2026-08-25)。serialは個体別
      (`434A124C5596` / `FBC18F0680B0` / `FC928F068181` / `0E028F0692F1`)で、
      `--probe VID:PID:Serial`で確定選択できる。ただし**同時attachは未確認**——下記の通り
      portが空かなかったため、1台ずつ切り替えて4台とも確認した
- [x] **「LinkEを同時に使えない」原因が判明**(2026-08-25)。udev権限でもtoolでもなく、
      **WSLの`vhci_hcd`がhigh-speed portを8本しか持たない**ため。
      この作業台では7本を別プロジェクトのCH343×4 + CH340×3が使っていた。
      埋まっていると`usbipd attach`が`WSL usbip: error: no free port`で落ちる。
      **WCH-Link固有の制限ではなく**、1本空ければ2台入る。
      `modinfo vhci_hcd`にparameterが無いので8本は増やせない。
      詳細は[upload-and-fixture.ja.md](upload-and-fixture.ja.md)
- [x] **probe切り替えtoolを追加**([`tests/manual/probe_switch`](../tests/manual/probe_switch/probe_switch.py)、2026-08-25)。
      `tests/.env`の`CH32_PROBE_<NAME>=<serial>`で名前を付け、名前かserial接頭辞で
      detach + attachする(約5秒)。**識別はserialのみ**——bus idは挿し直しで振り直され、
      COM番号は物理portに付くので挿し替えると変わる。外すのはWCH-Linkだけ
- [x] **4 familyでの実機sweep実施(2026-08-25)**。`probe_switch`で切り替えながら
      `smoke.py --sketch all`。**実機検証がV103の1 familyだけだった状態は解消**。

      | board | 結果 |
      |---|---|
      | CH32V103R8T6 | **12/12 pass** |
      | CH32V203C8T6 | **12/12 pass** |
      | CH32X035C8T6 | **12/12 pass** |
      | CH32L103C8T6 | **11/12** — `tone_selftest` FAIL |

- [x] **CH32L103で`tone()`が鳴らない原因を特定して直した**(2026-08-25、
      **未承認・実機検証済み**、[承認状態](approval-status.ja.md)へ要追加)。
      **CH32L103のTIM4は32 bitタイマ**(データシート「General-purpose TIM4 (32-bit)」、
      EVT `ch32l103.h`も`ATRLR_TIM4`/`CNT_TIM4`/`CHnCVR_TIM4`をunionで持つ)。
      coreは`ATRLR`を**16 bitストア**で書いていたが、32 bitレジスタへの16 bitストアは
      **上下両方のhalfwordへ複製される**——実測で`0x1F3F`を書くと`0x1F3F1F3F`になり、
      1 msのつもりが65秒周期になってupdate eventが来ない。
      TIM2/TIM3は16 bitなので正常、だから`analogWrite()`もServo(TIM3)も無事だった。
      32 bitストアに変えると`cnt_high_5ms=7994` / `irq_hits_5ms=5`で正常、
      実機の`tone_selftest`も**9/9 PASS**。
      対処: 変異体が`CH32_TONE_TIMER_BITS` / `CH32_SERVO_TIMER_BITS`を出し、
      `ch32_registers.h`の`CH32_TIM_ATRLR32()`を使い分ける。
      どのfamilyが32 bitタイマを持つかは`generate.py`の`WIDE_TIMERS`に手書き
      (下記の通りdevice-dataに機械可読な表がまだ無い)。
      **影響したのはtone()だけだが、対象はL103以外にもある**:
      EVTヘッダでCNTとATRLRがunionになっているのは
      `ch32l103.h` / `ch32v205.h` / `ch32v20x.h` / `ch32x3x5.h`の4 family、
      つまり**CH32L103 / CH32M103 / CH32V203 / CH32V205 / CH32X305 / CH32X315**で、
      いずれも`CH32_TONE_TIMER`が4。実機で確認できたのはL103だけ
      (V203C8T6はそのpartが16 bitなので元から通っていた)。
      **同じ修正でServoのバグも1件潰れている**: CH32V208はtone()がTIM5を取るので
      Servoに32 bitのTIM4が回っており、20 msのはずのフレームが65秒になっていた。
      board が無いので**未検証**だが、機構はL103のtone()で実測したものと同一
- [ ] `[P1]` `[要判断]` **「素直に使えない表」は上流に派生表を作ってもらう方向で検討する**
      (2026-08-25 maintainer)。device-dataはいまデータシートの**忠実な記録**として
      正しく、それは正しさの土台だが、**consumerが使いやすい形ではない**。
      抽出logicがconsumer側にあると、consumerごとに違う間違い方をする。
      実際に今日それが2件バグとして出た(32 bitタイマ / 一対多の後勝ち)。

      **いま生成器が持っている抽出logic**——これが「派生表があれば消せる」ものの一覧:

      | logic | 何をしている | 消せる表 |
      |---|---|---|
      | `UART/I2C/SPI/DAC/PWM_SIGNAL_RE` (5組) | signal名から周辺+instance+役割を正規表現で切り出す。タイマだけで`TIM1_CH1`/`T1CH1`/`T1C1`の3流儀 | **`pin_roles.csv`(既にある)** |
      | `PAD_PORT_RE` | `PA0-WKUP` / `PC13-TAMPER-RTC`のような装飾付きpad名からportとbitを取る | pad正規名の列 |
      | `WIDE_TIMERS` | どのfamilyのTIM4が32 bitか。**手書き** | タイマ表(下記) |
      | `route_remap_value()` / `*_ROUTE_ORDER` | route文字列から`remap-N`/`af-N`を判別し、優先順を持つ | route種別の列 |
      | 一対多の選択 | 193組でpadを1つ選んでいる。いまは「表の最後」 | 既定pad列(下記) |
      | `NON_PORT_PADS` / `UNUSABLE_PADS` | GPIOでないpad、使ってはいけないpad | errata / pad種別 |

      **`pin_roles.csv`は既に155c398に入っている**
      (`part_number,series,family,peripheral,role,pad,routing,signal`、23742行)。
      **これを読めば5組の正規表現が全部消えます。** 取り込み判断とセットで検討する。
      `pin_alternate.csv`(241行、family毎のper-pin AFのregisterとbit位置)もあり、
      これは**af-N方式をcoreが実際に設定できるようになる**材料
      (いまは「未対応」とNOTEを出しているだけ)。ただしaf-Nの実機は入手不能なので、
      入れても検証はcompileまで。

      **上流へ依頼したいのは4つ**(2026-08-25に155c398の全31表を確認した結果。
      他は既にあるか、こちらで導出できる):

      1. **タイマの表**。`family, timer, kind, counter_width_bits, channels,
         complementary, update_vector` あたり。**`systick.csv`と同じ形**で足りる
         (あれも`width_bits`と`write_bits`を持っている)。
         いま`features.csv`に「General-purpose TIM4 (32-bit)」という**文**が
         series粒度であるだけで機械可読ではなく、`WIDE_TIMERS`が手書きなのはそのため。
         **今日のtone()のバグを唯一防げた表**
      2. **pad名の正規化列**。`pin_roles.csv` / `pin_functions.csv`の`pad`は
         `PA0-WKUP`(432行) / `PC13-TAMPER-RTC`(89行)のような装飾付きが混ざる。
         **`pin_alternate.csv`は既に`pad,port,pin`を持っている**ので、
         新概念ではなく揃えるだけの話。装飾が増えたことに気付かないまま
         PC13/PC14/PC15が現れたのが今回の件
      3. **flashの幾何の表**。消去単位(page size)・書き込み粒度・fast erase
         の有無がfamilyごとに違い、いまは容量(`flash_bytes`)しか無い。
         `EEPROM`相当を作らないとしても、低レベルflash APIの前提
         (2026-08-25追加、[調査](research/system-api-esp32-style.ja.md))
      4. **既定padの印**。一対多の`(部品, 周辺, 役割, 経路)`が上流計測で984組
         (我々が読む範囲で193組)。`pin_roles.csv`に`preferred`列が1つ付けば、
         「普通はこれ」を**データシートを見ている側が1回決める**ことになる。
         consumerごとに推測すると、consumerごとに違う間違い方をする

      **依頼しないもの**(既にある / 導出できる):

      | 欲しかったもの | 答 |
      |---|---|
      | signal名→周辺・instance・役割 | **`pin_roles.csv`**(23741行)。5組の正規表現が消える |
      | per-pin AFのregisterとbit | **`pin_alternate.csv`**(240行)。af-Nを実際に設定できる |
      | flashの分割 | **`memory_configs.csv`**(67行) |
      | **SWD padを避ける** | **導出できる**。`pin_roles.csv`の`peripheral=SDI`が`SWCLK`/`SWDIO`で、**26 seriesを網羅**している。`peripheral=SYS`が`NRST`/`BOOT0`/`BOOT1` |
      | GPIOでないpad | `pins.csv`の`kind`。ただし`HO3`/`LED0`が`gpio`扱いなので、pad名が`P<port><bit>`かで見る今の判定のほうが確実 |
      | 使ってはいけないpad | `errata.csv`(既に`UNUSABLE_PADS`が参照) |

      **`preferred`列が来る前でも、SWD/BOOT padの除外はこちらで実装できる**——
      上の`SDI`/`SYS`から引ける。一対多の選び方を決めるときの前提条件になる

      **原則として言うなら**: 「データシートに何と書いてあるか」の表と、
      「coreは何を知る必要があるか」の表は別物で、後者を上流に1つ置くほうが、
      consumerごとに導出するより間違いが少ない。どこまで上流に持ってもらうかは判断が要る
- [ ] `[P2]` **(上流へ依頼)** device-dataに**タイマのbit幅の表**が欲しい。
      いまは「General-purpose TIM4 (32-bit)」というデータシートの機能表の文が
      生成READMEに出るだけで、機械可読ではない。`generate.py`の`WIDE_TIMERS`が
      手書きなのはそのため。`systick.csv`と同じ粒度でよい
- [x] **CH32X035の`CHnCVR`は32 bitではない**(2026-08-25、実機で確認)。
      `ch32x035.h`が`CH1CVR_R32`をunionで持っているのは**別アクセス手段**であって
      幅ではなかった。16 bitで`0x1234`を書いて32 bitで読み返すと
      TIM1/TIM2/TIM3のCHnCVR・ATRLR・CNTすべて`0x00001234`——**複製されない**。
      よって`analogWrite()`のdutyは正しい。
      (TIM3のCH3/CH4だけ書けないが、X035のPWM表はTIM3をCH1/CH2しか使っていないので影響なし。
      TIM4は全レジスタが0のまま=X035にTIM4は無い。L103では書けたので、
      この読み返しは「その周辺が居るか」の判定にも使える)
- [ ] `[P2]` **attach直後の`smoke.py`が「chip not identified」で落ちる**
      (2026-08-25、sweepのV203で発生)。CDC interfaceが先に見えてvendor
      interfaceがまだ、という窓に単発のprobe-rs呼び出しが当たる。
      `probe_switch`側は3回再試行するので、`--no-identify`を付けずに
      挟めば回避できる(sweepはそう直した)。`smoke.py`自身も1回再試行して
      よいかもしれない
- [ ] `[P1]` `board-identify`にCH32 probeを追加(ESP32のMAC読み出しに相当するのはtarget UID読み出し、Q-042)
- [ ] `[P1]` **(要判断)** logic analyzerを16 channelにするか。
      v1.0の7周辺を同時に観測するには12本要り、**8chでは足りない**。部材とconnectorの変更コストが最も高い
- [ ] `[P1]` **(要実機)** fixture配線の確定(Q-050)。X035はSWD=PC18/PC19、USB=PC16/PC17、CC=PC14/PC15が固定
- [ ] `[P1]` X035のI2Cはロット依存で使えない個体がある(`x035-adc-ch-i2c-unavailable`、
      下から5桁目=0)。**fixture inventoryにロット番号を記録**し、ADC試験はch3/7/11/15を避ける
- [ ] `[P2]` 複数DUT化。1 LinkE + mux か 1 lane 1 LinkE かはprobe識別の結果次第(Q-043)
- [ ] `[P1]` `[要判断]` **開発機材のリストアップと、常時つなぐ4台の選定**
      (棚卸し 2026-08-25、所有状況はmaintainer申告)。
      USB/IPのportの都合で**常時つなげるのは4台**(上の8 port制限の項)。
      入れ替えながら進める運用は確定。

      | 型番 | 手元 | 備考 |
      |---|---|---|
      | CH32V103R8T6 | **接続中** | |
      | CH32V203C8T6 | **接続中** | |
      | CH32X035C8T6 | **接続中** | |
      | CH32L103C8T6 | **接続中** | |
      | CH32V003 | **所有** | 未接続。極小RAM(16K/2K)の唯一の代表 |
      | CH32V307 | **所有** | 未接続。大容量・USART 5本・vector別 |
      | CH32X033 | **要確認** | 持っている気がするが配線が無い |
      | CH32M030 | 公式board未発売 | サードパーティの試作基板はある |
      | CH32H417 / V002 / V006 | **購入予定** | 公式boardあり |
      | CH32M103 / CH32X315 | **未発売** | |

      **coreの構造の軸と、それを埋められる型番**:

      | 軸 | 埋める型番 | 現状 |
      |---|---|---|
      | 極小RAM | V003 | **所有済み——挿せば埋まる** |
      | 大容量 / USART 5本 | V307 | **所有済み——挿せば埋まる** |
      | 32 bitタイマ | L103 | 接続中(tone()の件でここが効いた) |
      | Serial既定がUSART2 | X033 / M103 / X315 | X033の確認待ち。他は未発売 |
      | af-N(per-pin AF、core未対応) | V205 / X305 / X315 | **入手不能**(下記) |
      | Mシリーズ | M007 / M030 / M103 | **入手不能**(下記)。M030の試作基板のみ |
      | 無線 | V208 | 未所有 |

      **af-NとMシリーズはデータシート先行で、ほぼ未発売かチップのサンプル出荷まで**
      (2026-08-25 maintainer)。つまりこの2軸は**努力ではなく入手性で塞がっている**ので、
      当面`[compile only]`のままになる。**「未検証」は恒久的な状態として扱い、
      利用者に誤解させない書き方をする**ほうへ倒す
      (variantのaf-N instanceには既にNOTEが出ている)。
      **いま効率が良いのはV003とV307**だが、**物理的な差し替えは当面しない方針**
      (2026-08-25)。4台の中でのattach入れ替えで進める。
      なお`usbipd`が覚えているWCH-Linkは**6本**で、うち2本
      (COM20 `F90E8F067DFD`、COM24 `38EF8F06BDC2`)は現在未接続
- [ ] `[P1]` `[要判断]` **CH32H417を買うなら、その前にboard定義が要る**。
      device-dataには`CH32H415` / `CH32H416` / `CH32H417`があるが、
      `SERIES_CONFIG`に**無い**(24 board / 27 series)。理由は`loadcode` bootで
      startup等価性harnessが対応していないため(下の`[P2]`項)。
      **買ってから気付くと使えないので、優先度を上げるか購入を後回しにするかの判断**

## テスト基盤

- [x] **テスト計画を作成**([tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) / [英語](../tests/TEST_PLAN.md))。
      board階層は**未承認の提案**([承認状態 A-3](approval-status.ja.md))。
      自動/手動の切り分け、board階層(A/B/C/D)、ペリフェラル別の検証方法4種、
      Board Manager配布物としての検証項目
- [x] `tests/hardware/` → **`tests/manual/`**へ移動し、`chip_info.py`(chip/probe/port/FQBN判定)を追加。
      `test_`プレフィックスを付けないので`pytest`一括実行に混ざらない
- [x] **`tests/`直下の12本の`test_*.py`をカテゴリのディレクトリへ移した**(2026-08-22)。
      `generated/` `vendor/` `startup/` `compile/` `sizebench/` `package/`
      `sketches/` `unit/` `manual/`の9つで、入口(`test_*.py`)とharnessを同居させる。
      規約そのものは[`unit/test_tests_layout.py`](../tests/unit/test_tests_layout.py)が
      検査する——直下に実行可能ファイルを置かない、宣言のないディレクトリを作らない、
      `manual/`に`test_`を付けない、同じファイル名を2箇所に置かない、
      **収集されるconftest.pyは1つだけ**。
      `dist/`という名前は避けた。**pytest既定の`norecursedirs`に`dist`が入っている**ので、
      除外リストを一行簡略化した日にinstall testが静かに消える
- [x] **収集されるconftest.pyを2つにすると壊れることが分かった**(同日、実際に踏んだ)。
      pytestは全conftest.pyを`conftest`という同じmodule名でimportするため、
      `tests/sketches/conftest.py`を置いた瞬間に`tests/conftest.py`がsys.modulesから
      消え、`from conftest import load`をしている7本が
      `ImportError: cannot import name 'load'`で落ちた。**どちらのファイルも
      エラーには出てこない**。sketch側のhost実装は`sketches/testcmd.py`という
      普通のmodule名にし、`tests/conftest.py`が`tc` fixtureとして配る形にした
- [x] **`pytest --clean`が全体に効くようにした**。`--clean`は
      `pytest-embedded-arduino-cli`のoption(本来は`arduino-cli compile --clean`)。
      `tests/conftest.py`が相乗りして`.pytest_cache`・`__pycache__`・
      前回のscratchディレクトリも消し、header行に消した数を出す。
      `.tools`と`~/.arduino15`は**消さない**——結果は変わらず実行が1時間伸びるだけ
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
      ([tests/sketches/test_sketch_profile_build.py](../tests/sketches/test_sketch_profile_build.py))。
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
      (package indexの正本)から取り、SHA-256照合つき。device-dataはlocked commitで
      checkout(当初は`boards.txt`のヘッダ、現在は`vendor/ch32-device-data.lock.toml`)。
      `CH32_*`は上書き用として存続。CIも同じ経路へ移行
- [x] **device-dataのpinを`vendor/ch32-device-data.lock.toml`へ集約した**。
      それまでcommit idが生成物55ファイルのヘッダに入っていたため、**中身が1バイトも
      変わらないupstream bumpでも56ファイルの差分**になり、レビューが成立しなかった。
      lockはgenerator自身の出力なので`generated-sync`がそのまま検証する。
      lockは**generatorが読む表のSHA-256も持つ**(`errata` `pin_functions` `pins`
      `products` `remap_fields`の5つ)。読み口を`read_table()`一本にしたので、
      入力を増やせば自動でlockに載る。実測: `66a421f`→`0a1eed7`の8 commitのうち
      **この5表に触れたのは2つだけ**で、bumpの差分は56ファイルから
      「lock 3行 + CH32V305の`pins_arduino.h`」になった
- [x] **CH32V305のvector tableが間違っていたのを直した**。`SERIES_CONFIG`が
      `v307_d8`にしていたが、`ch32v30x.h`のコメントは
      `#define CH32V30x_D8C /* CH32V307x-CH32V305x-CH32V317x */`で、
      `evt_variants.csv`も同じ。D8側はslot 84/85(`USBHSWakeup`/`USBHS`)が
      予約のままなので、**V305の看板機能であるUSB-HSにベクタが無かった**。
      サイズ変化なし(RSVもIRQも`.word`1つ)。同種の取り違えを防ぐため、
      `vectors`の接尾辞を`evt_variants.csv`の既定macroと突き合わせる検査を入れた
- [x] **WCH-Linkのファーム2.11では、probe-rsのdownload後にコアが走らなかった**。
      書き込み自体は成功(`Finished`)するのに`--reset`が効かず**haltのまま**で、
      `disable_debug_module: could not clear sw-breakpoint state: DtmOperationFailed`
      が出る。**ファームを2.12へ上げたら解消**(同じ無印CH549のまま。LinkEも2.12)。
      警告は2.12でも出るが無害。**プローブ種別ではなくファーム版の問題**だったので、
      uploadラッパーの同梱もrelease archiveの変更も不要で、ADR-0008はそのまま。
      **症状がエラー無しの「書けたのに動かない」**なので原因に辿り着けない点は変わらず、
      古いファームのLinkを持つ利用者は同じ目に遭う。
      SDI printがLinkE 2.10以上を要求するのと同じ扱いで、**最低ファーム版を文書化**するか、
      uploadの前に`wlink status`が報告する版を見て警告するかは**要判断**。
      この一件が、CH32V103の「全sketchがnothing received」と
      `uart_scan`の「どの経路も届かない」の**両方の正体**だった。配線もコードも正常。
      皮肉なことに、これで実機シリアルが使えずレジスタ読みに追い込まれた結果、
      **CH32V103のSysTickバグ(P0)が見つかった**
- [x] **`smoke.py`が前のsketchの出力を読むことがある**(2026-08-22、CH32V103で顕在化。
      **同日、コマンド規約の導入で解決**——下記「コマンド規約」を参照)。
      手順は「ポートを開く→`reset_input_buffer()`→upload→`seconds`秒読む」だが、
      **`reset_input_buffer()`はhost側のバッファしか捨てず、probe内のFIFOは残る**。
      さらに前のsketchは**upload中ずっと出力し続ける**(CH32V103は書き込みに7〜10秒)。
      結果、読み取り窓が前のsketchの出力で埋まる。
      実際に`heap_string`が`core_api`の行を、`serial_println`が`route_selftest`の行を
      読んでいた。**1本ずつ実行すれば通り、連続実行で全滅する**ので誤診しやすい。
      X035/V203/V307で表面化していないのは書き込みが速く窓に間に合っているだけで、
      **9/9 PASSは「たまたま間に合った」可能性がある**。
      失敗の向きは偽陰性(期待文字列が無い→FAIL)なので、**PASSした結果は有効**。
      **試して駄目だった対処**: (1)upload前のdrain——upload中に吐くので無意味、
      (2)upload後のdrain+`probe-rs reset`——**本命の頭を食べる**(3 PASS)、
      (3)リセットの非同期化——変わらず(3 PASS)、
      (4)drain廃止+窓を12秒——**4 PASSまで改善**したが5本落ちる。
      捨てるべき古い出力と残すべき新しい出力が**同じ経路を通る**ので、
      時間だけでは区別できないのが本質。投機的な変更は残さず一旦committed状態へ戻し、
      **受信側の工夫ではなくプロトコルで解いた**(次項)
- [x] **実機testのコマンド規約を導入した**(2026-08-22)。
      [`tests/sketches/testcmd.h`](../tests/sketches/testcmd.h)(target)と
      [`testcmd.py`](../tests/sketches/testcmd.py)(host)。全11 sketchを載せ替え。
      仕様は[tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md)。
      `setup()`は`tc_begin()`(=`Serial.begin`と`"<name> READY"`)だけ、
      判定は`loop()`が`RUN`を受けてから走らせる。
      **同期の要は`PING <token>` → `PONG <token>`のtoken**。
      `READY`だけでは足りない——全sketchが`PING`に答える以上、
      **前のsketchが残した`PONG`が今の`PING`の答に見える**。
      ホストがいま決めた数字は、それ以前に作られた出力には入っていない。
      副次的な効果が2つある。
      (1) `setup()`で20秒かかるtestが「起動しないboard」と区別できる。
      (2) `hooks_selftest`/`serial_echo`が`smoke.py`でSKIPされなくなった(下記)。
      `arduino-cli`はsketchフォルダの外をコンパイルしないので、
      `testcmd.h`は**各caseへコピーを配る生成物**にした
      ([`sync_testcmd.py`](../tests/sketches/sync_testcmd.py)、
      `--check`は`generated/test_generated.py`が回す)。
      代替(`../testcmd.h` / `-I`をbuild propertyで / caseごとにsymlink)は
      それぞれbuildディレクトリの位置・4つのcallerの取りこぼし・Windowsで破綻する
- [x] **WCH-LinkのUARTブリッジは「送るものがある」ときにしか吐かない**(2026-08-22実測)。
      規約のバナーを「最初のコマンドを受けたら止める」形にしたら
      (参照実装の`~/dev`はそうしている)、**11本全部が行の途中で止まった**——
      `heap_string`はホストに`string=ab`まで届いてそこで無音。
      バナーが管を動かしていたと分かったので**止めない**ことにした。
      あちらの参照実装はMCU内蔵のUSB-CDCで、こちらはブリッジという違い。
      代償は、誰も読んでいないポートへ喋り続けるとブリッジが溢れて
      **出力が混線する**こと(`hooks_selftest READY`が`selftest READY`と`hooY`に割れ、
      待っている文字列が連続して現れない)。
      これはボードを黙らせるのではなく、**`smoke.py`がuploadを跨いでポートを
      開いたままにする**ことで解いた。
      さらに**flashのあと配送が止まる**ことがある——バナーが2本来て36秒間無音、
      数分前に同じbuildが通っていても起きる。**portを閉じて開き直すと直る**ので、
      `smoke.py`は「uploadを跨いで開いたまま → 終わったら閉じて開き直す」の2段にした。
      往復も遅く、`PING`→`PONG`に約5秒かかる。handshakeは12秒、
      replayの1ステップは最低10秒にしてある。
      3点とも[docs/upload-and-fixture.ja.md](upload-and-fixture.ja.md)に実測値つきで残した
- [x] **`hooks_selftest`の`serialEvent()`がコマンドを食べていた**(実機で発覚)。
      `main()`は`loop()`のあとに`serialEventRun()`を呼ぶので、
      無条件にdrainするhookは`tc_ready()`と同じバイトを奪い合い、
      **バイトが2つの呼び出しの間に届いたときは必ず勝つ**。
      ホストの`PING`がそこへ消えて「banner but no PONG」になっていた——
      RXは完全に正常なのに。
      hookは`waiting_until`が立っている間だけ読むようにし、
      **改行まで読んでから**報告するようにした(数バイトずつ届くので、
      「availableが尽きたら終わり」だと行の尻尾が残って`unknown cmd`になる)。
      待機中は`tc_ready()`ではなく`tc_tick()`を呼ぶ——
      入力に触れずにバナーだけ出す。上の癖1のため、黙ると配送が止まる
- [x] **コマンド規約をCH32V103実機で検証した。11本すべて`failures=0`**(2026-08-22)。
      **これまで全boardでSKIPしていた`hooks_selftest`と`serial_echo`を含む**。
      3本(`hooks_selftest`/`servo_selftest`/`wire_selftest`)は
      `no banner → probe-rs reset → PASS`で、後から入れたreset経路が効いている
      (毎回表示するので、頻度が上がればbenchの異常として見える)。
      過程で**実機でしか出ない欠陥を3件見つけた**。
      (1) `core_api`の期待順が印字順と違っていた——`fmt=FF,-42,1.50`は
      `availableForWrite`より前。pexpectも前方一致なのでpytest側でも落ちる本物の誤り。
      (2) `hooks_selftest`の`serialEvent()`がコマンドを食べていた(下記)。
      (3) 待機中にバナーが止まるとブリッジの配送が止まる(下記)
- [x] **`gpio_loopback`もコマンド規約へ載せ替えた**。同じ壊れ方をしていた——
      `dut`はfunction scopeなので、`setup()`で全部出力して1行ずつ`expect`する形は
      ファイル先頭の1本しか通り得ない。`testcmd.h`の配布対象を
      `manual/*/sketch.yaml`にも広げてある。
      X035とV307でcompile確認(V003は`PA0`/`PB0`が無く**変更前から**compileできない。
      padは`.env`で指定するもので、既定値は作者のX035配線)
- [x] **`crt0_probe`をコマンド規約に載せた**(2026-08-25、CH32X035実機で5 pass)。
      markerはもともと大域constructorが変数へ記録しているので、印字だけ`RUN`待ちに
      すれば済んだ。載せた理由は「最初の出力を取り逃す」ではなく——このtestは
      driverが自分でresetするので元からその問題は無い——**言うだけ言って黙るsketchは
      最後の1行をWCH-Linkのブリッジに置き去りにする**ほう。
      固定時間の読み取りも消え、バナーを待って`RUN`を送る形になった。
      `bss_zeroed`/`data_copied_from_flash`/`init_array_ran`はboard側の`tc_check`、
      **対照の`past_ebss`だけはhost側**(パターンを決めているのがhostなので、
      boardにも定数を置くと二重管理になる)。

      最初の版は**V103だけ落ちた**。bannerは来るのに`RUN`の応答が10秒来ない。
      原因は「`RUN`を撃つのが早すぎた」で、transcriptを見ると
      **PONGが返るまでにbannerが7回**出ている——WCH-Linkのブリッジが
      最初の1往復に数秒かかる(`smoke.handshake`のコメントが既に書いていた話)。
      `smoke.handshake()`(`PING <token>`→`PONG <token>`)を先に通し、
      `RUN`は20秒×2回に。以後4 boardとも安定
- [ ] `[P1]` **コマンド規約を実機で検証していない**(2026-08-22時点)。
      全11 sketch × 6 boardのcompileは通っているが、
      作業台のCH32V103が**flashではなくSRAMから起動している**らしく
      (`probe-rs run`が`Breakpoint(Software)` @ `0x20000018`=SRAM領域を報告、
      SysTick CNTは0のまま、UART出力なし)、**committed版のsketchでも同じ**。
      よってコード起因ではなくBOOTジャンパ等の物理状態。
      probeのUSB再接続(`usbipd.exe detach`/`attach --wsl`)では戻らない。
      **boardを見られる状態になったら`smoke.py --sketch all`を回す**
- [x] **`uart_scan`がremapを要する経路を1つも試せていなかった**。
      `load_remap_fields`が`(series, kind, index)`の3要素キーを返すのに
      `remap.get((series, index))`と2要素で引いていて常に`None`。
      `if value and not bits: continue`で**全ての非default経路が捨てられていた**。
      I2C/SPIセレクタを追加してkind次元が入って以降、**全boardでdefault経路しか
      スキャンしていない**。CH32V103で3経路→5経路(`U1-PB6`と`U3-PC10`が追加)になった。
      「どの経路が配線されているか」を答えるための道具が、
      **remap先を一度も試せていなかった**ことになる
- [x] **CH32V103の`millis()`/`micros()`/`delay()`が動かなかったのを直した**(P0)。
      SysTickのレジスタ配置がV103だけ違う。WCH自身の`core_riscv.h`より:

      | offset | CH32V103 | 他の10 family |
      |---|---|---|
      | +0x00 | CTLR | CTLR |
      | +0x04 | **CNTL0..3**(カウンタ下位) | SR |
      | +0x08 | **CNTH0..3** | CNT |
      | +0x0C | **CMPLR0..3**(比較値 下位) | — |
      | +0x10 | **CMPHR0..3**(比較値 上位) | CMP |

      違いは5点: 比較値の位置、カウンタの位置、**SRが無い**、**CTLRは`STE`のみ**
      (`STIE`/`STCLK`は存在せず、割込み許可はPFIC側だけ)、**クロック源がHCLK/8固定**。
      さらに**カウンタはバイト単位でしか書けない**(word書き込みは無視される)。
      読みはwordで可(EVTのdelayループがそうしている)。
      出典: WCHの`core_riscv.h`、EVTの`SYSTICK/SYSTICK_Interrupt`、
      旧コア`ch32-riscv-arduino`の`cores/CH32V103/port.c`(EVTを出典として明記)。
      **実機検証(CH32V103R8T6 @72 MHz)**: `CMPLR`=8999(=72e6/8/1000-1)、
      millisが実時間の**96%**で進行(残り4%は測定時のhalt分)、
      `servo_selftest`が完走して`failures=0`、PCはmainループ内。
      シリアルが読めない基板なので、すべてレジスタとRAMの読み出しで確認した。
      **影響はCH32V103シリーズのみ**。他10 familyはサイズがバイト単位で不変
- [x] **PLLに対応し、CH32V20x / CH32V307を8 MHz -> 144 MHzにした**。
      仕組み: `clock_configs.csv`と`clock_symbols.csv`から**生成時に設定を解決して**
      boards.txtへ出す(`CH32_CLOCK_SYSCLK_HZ` / `USE_PLL` / `PLL_MASK` / `PLL_VALUE` /
      `EXTEN_ADDR` / `EXTEN_BITS`)。`condition`がdie依存なので、series/pnum粒度の
      boards.txtで解決すれば`#if`が要らない。AHB分周は`SYSCLK / F_CPU`から導出する
      ままなので「F_CPUが唯一のつまみ」も維持。
      **既定は96 MHz**(144ではなく): ADCPREは最大/8でf_ADCが14 MHzなので、144だと
      ADCが18 MHzになり規格内に入れられない。96なら/8で12 MHz、USBもPLLCLK/2で48 MHzが出る。
      **実機確認: CH32V307VCT6とCH32V203C8T6の両方で144 MHz動作。さらに
      CH32V307VCT6は96 MHzで実行可能な9 sketchすべてPASS**
      (`serial_println`が化けない = PLLが噛んでいてPCLK2 == F_CPU)
- [x] **PLL関連で踏んだ罠を2つ記録**。(1) `RCC_PLLMULL18_EXTEN`は**値が0**なので
      「PLL値が非0ならPLLを使う」判定は成立しない。`CH32_CLOCK_USE_PLL`で明示する。
      D8C(V305/V307/V317)が黙って8 MHzのままになるところだった。
      (2) クリアすべきPLLフィールドのマスクはfamilyごとに違う
      (V103/V20x/V30x/L103は4bit、**V205は5bit**、**V407は位置違い**、V307は同じ
      registerに`PLL2MUL`/`PLL3MUL`が同居していて巻き込めない)。設定に出てくる記号から
      所属フィールドを引いて和を取る。一方`PLLON`=1<<24 / `PLLRDY`=1<<25 / `SW`=0x3 /
      `SWS`=0xC / `SW_PLL`=0x2は**全familyで一致**を確認したのでコアに直接置いた
- [x] **APB1を`/1`にした**(PPRE1=1、PCLK1 == PCLK2 == HCLK == F_CPU)。
      EVTはV103/V20x/V30x/L103で48 MHz以上を`/2`にするが、**24 MHzだけ`/1`**で、
      これはSTM32F1のAPB1 36 MHz上限とちょうど一致する。datasheetは全familyで
      `F_PCLK1 max == F_HCLK max`(V103 80 / L103 96 / V20x・V30x 144)。
      STM32F1形でないV205(192 MHzまで`/1`)とV407(480 MHzまで`/1`)は分周しない。
      **実機で144 MHzのまま`wire_selftest`が全項目PASS**(I2CはAPB1、fast mode含む)、
      tone(TIM7=APB1)のタイミング系も全PASS。これで`PCLK == F_CPU`の前提が
      7か所そのまま生きる。EVTがなぜ`/2`かは上流へ確認を出す
- [x] **`servo_selftest`のハングを直した**。ISRの中から`SWEVGR = UG`(更新イベント)を
      撃っていたのが原因で、**ハンドラが処理すべきフラグを自分で立て直す**ため抜けられない。
      デバッガでPCを3回サンプルすると毎回`TIM6_IRQHandler`の中、`millis()`は動くが
      **本来の1/5の速度**、TIM6の`INTFR`はUIFが立ったまま(PSC=95→1 MHz、
      ATRLR=18499→18.5 msはどちらも正しい)。
      同じfamilyで動いている`tone`と比べると、**toneはUGを起動時に1回だけ、
      しかも割込みを有効化する前**に撃っていた。`timer_set()`からUGを外し、
      ISRではATRLRを書くだけにした(次の周期から自動で反映される)。
      **CH32V307VCT6 @96 MHzで全10項目PASS**
- [x] **`hooks_selftest`と`serial_echo`がどのboardでもSKIPされていたのを解消**。
      `smoke.py`はlisten専用だったので、`dut.write()`で対象を駆動するtestを
      再現できず飛ばしていた。コマンド規約の導入で駆動できるようになったので、
      `smoke.py`は**各caseの`test_<case>.py`をASTで読み、`dut.write` /
      `dut.expect_exact` / `dut.expect`の並びをソース順に再生する**ようにした。
      testが1関数の素直なscriptなのでそのまま写せる。
      判定の正本が1箇所のままなので、sketchを足してもsmoke側は変更不要
- [x] **`tone()`の無効ピン規則を直した**。`noTone()`が
      `pin == tone_pin || !digitalPinIsValid(pin)`で止めていたため、
      **存在しないピンを渡すと別ピンで鳴っているtoneが止まっていた**。
      `tone()`自身が2行上で「別ピンで鳴っているtoneが優先」と書いているのに、
      無効ピン経由でその規則を迂回していた。無効ピンは何もしないのが正しい。
      **CH32V307VCT6 @96 MHzで全9項目PASS**
- [ ] `[P1]` **CH32V30xのflash/SRAMは利用者が構成を選べる**(datasheet注記)。
      256K+64Kの製品は(192+128)/(224+96)/(256+64)/(288+32)から選べ、
      ロットによっては(128+192)も。FLASHはゼロウェイト領域R0WAITを指し、
      非ゼロウェイト領域が(480K-R0WAIT)バイト。
      **`products.csv`は480K/64Kを1組だけ`confirmed`で持っていて「この型番はこう」と
      読める**ため、linker scriptを書くconsumerを誤らせる。上流へ組み合わせ表の依頼を出す。
      実測(CH32V307VCT6): ESIG `0x1FFFF7E0`=`0x0120`(288KB、wlinkの表示元)、
      USER option byte `0x1FFFF800`=`0xBF`、**SRAMは64Kが実在・非ミラー**
      (0x20000000/0x20008000/0x2000FFF0に別値を書いて3つとも保持)。
      3つの情報源が食い違うので、RMのオプションバイト表(`SRAM_CODE_MODE`の符号化)が要る。
      **文書に無い部分(ESIGが288を返すのにSRAMが64Kある理由)は上流では調べられない**ので、
      基板のあるこちらで測る。実害は「288K+32K構成の基板でスタックがRAM外」だけ
- [x] **CH32V203RBT6だけdie variantが違う問題を直した**(`CH32V20x_D8`、他のV203は`D6`)。
      D6とD8は**slot 61から並びが違い**(D6は`UART4`、D8は`ETH`)、D8は69 slotで
      D6は62 slot。`CH32_IRQN_UART4`が61と66でずれていた。
      対応: vector tableの選択を`build.vector_variant`という**1つのstem**に集約し、
      platform.txtが`vectors_*.inc`/`irqn_*.h`/`exti_*.h`の3つを組み立てる形にした。
      pnum項目が1行上書きするだけでdie variantを差し替えられる。
      上書き対象は`evt_variants.csv`由来で**手書きしない**。
      `ANY`はboardの既定(d6)を保つ——既にseries最小のflash(32K/10K)を宣言していて
      「特定の石向けではない」項目なので、RBT6の人はどのみちpnumを選ぶ必要がある。
      series内でdieが割れるのは**全seriesでCH32V203だけ**(確認済み)。
      サイズはCH32V203RBT6だけ**956→984バイト(+28)**。D8のvector tableが7 slot長い分で、
      他の121ターゲットは1バイトも動かない——上書きが効いたことの裏付けにもなっている
- [x] **手書き定数のうち、上流が答えられるものをデータ由来にした**。
      `CH32_HSI_HZ`(`operating_conditions.F_HSI.typ`)・`CH32_HPRE_LINEAR`
      (`clock_prescalers`のHPRE `/2`が`0x10`か`0x80`か)・`CH32_GPIO_PORT_WIDTH`
      (解決済みpad集合の最大bit+1)の3つ。11 familyすべてで手書き値と一致し、
      **生成物は1バイトも変わらなかった**。`flash_latency`と`vectors`は値を
      手元に残したまま表と突き合わせる(不一致でgeneratorがexit 1)。
      `march`/`mabi`(任意拡張をどこまで有効にするか)・`f_cpu`・`adc_bits`・
      `i2c_has_rtr`・`systick64`・CSR初期値は据え置き。理由は`FAMILY`のコメント
- [x] **device-dataの取り込みは「リリース準備の最初の工程」で手動でのみ行う**と決めた。
      取り込みだけ先に進めるとリリース物とずれるため、**週次等の自動化はしない**。
      手順は[generate.pyのREADME](../tools/generate/README.ja.md)、
      `release.yml`のヘッダからも参照。`--check`が末尾に出す採用サマリ
      (`N additive, M rewriting existing lines`)で、既存の値が動く変更だけを
      重点レビューする。`--diff`でunified diffも出せる
- [x] **リリース物にデータの版は載せない**と決めた。installした人が版を知る必要はなく、
      **リポジトリを見れば分かる場所があればよい**という判断。その場所は
      `vendor/ch32-device-data.lock.toml`で、README(構成表)・`docs/handoff.ja.md`・
      生成物ヘッダの3か所から辿れる。release archiveに`vendor/`を入れる案と
      リリース本文に出す案は、どちらも取り下げ
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
- [x] `.env`の項目を増やした(2026-08-25)。probeのserialは`CH32_PROBE_<NAME>`で
      名前付きにして`probe_switch`が読む。`CH32_PROBE`(選択)と`CH32_PORT`は既にあった。
      `.env`はコミットされないので、この作業台のserialがrepositoryに入らない
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
