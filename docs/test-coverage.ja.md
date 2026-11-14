# 実装・テストカバレッジ

[テスト計画](../tests/TEST_PLAN.ja.md)の保証対象を、実装と既存テストの両面から整理します。
これはソースを照合した棚卸しです。この文書の作成時に実機を実行したという意味ではなく、
過去の PASS、全対象での保証、コード行カバレッジを示しません。

## 読み方

実装は「あり」「一部」「未実装」を区別します。機能は基本すべて作ります（作らないのは特殊なもの、今は BLE だけ。2026-11-14 ユーザー）。実装が無いことを「対象外」とは書きません。
「あり」でも silicon/part/route による条件があります。全系列で動くという意味ではありません。
テスト定義は「あり・限定」「間接利用」「ビルドのみ」「なし」を区別します。
「なし」は専用の動作検証を確認できないものです。examples のコンパイルは動作検証に含めません。
不足欄が新しい契約の候補であり、既存テストを残す義務ではありません。

実行カバレッジは別 artifact で contract × target × environment × observation の結果を記録します。
実機の結果の集計は [tests/coverage.toml](../tests/coverage.toml)（何をどの範囲で確かめるか）と
[tests/coverage.py](../tests/coverage.py)（`run_hardware.py` の結果との突き合わせ）で行います（使い方は
[tests/README.ja.md](../tests/README.ja.md) の「カバレッジ」）。
特定系列のテストファイルの存在を、その系列の実機 PASS に変換しません。
リリース状態や、意味のない機能数・テスト数・割合は持ちません。

## workspace の実行検査

通常のpytestとCIはunit/・platform/の実機不要検査です。
実機契約は tests/run_hardware.py の case で、case ごとの試験・観測・保証しないものは
[tests/README.ja.md](../tests/README.ja.md) の「実機契約」の表が正です（ここには写しません）。
系列ごとに何が通ったかは、coverage.py の報告（最新の実行の結果）で見ます。

| 入口 | 保証するもの | 保証しないもの |
|---|---|---|
| tests/unit/test_workspace.py | 通常の収集範囲、Pythonテストとsketchの分離、旧 harness との分離 | コア API の動作 |
| tests/unit/test_core_bringup_safety.py | 復元 prefix の保存性、書込み不能領域に非 blank がある場合の拒否 | 実際の消去・復元、実機動作 |
| tests/unit/test_maintenance_tools.py | profile 同期、version 更新の対象限定、package 入力の自己完結、CI の旧 suite 非依存 | CH32 compile、実機動作 |
| tests/platform/test_source_locks.py | ArduinoCore-API、lwIP、Mbed TLS のファイル一覧と lock の hash のオフライン一致 | upstream commit の由来、compile、実機動作 |
| tests/unit/test_bench_place.py | アドレスの形、プローブの見つけ方、記録が別の個体を指すときの拒否、型番の候補の選び方、記録の検査、流す case の選び方、電源を切る口がプローブの口だけであること | 実際の実機操作 |
| tests/unit/test_coverage_report.py | coverage.toml の項目が実在の case を指すこと、最新の実行と過去の最良の出し方、n/a のスキップ | 実機の結果そのもの |

## 現行 CI の保証と未整備の検証

現行 CI は新 suite、device-data 再生成、example profile の同期、vendor provenance、隔離 package 導入を実行します。
package 確認は独立ツール内の compile-only source を使い、旧ベンチ sketch を読み込みません。

全型番 compile matrix、全 example × profile の compile、startup 等価性、割り込み表比較、size baseline、
製品 board compile、recipe/udev/chip database の個別契約は、新しい実行入口が未整備です。
旧 job は呼び出しません。新 suite が成功しても、これらが検証されたとは扱いません。
実機契約は通常のpytestの収集対象にせず、tests/run_hardware.pyで接続先を明示します。
明示実行用の [core bring-up](../tools/diagnostics/core-bringup/README.ja.md) は、
V205RCT6/X315MCU6 の LinkE・RVSWD のみで、初期化・constructor・DMSEQ 要求/応答・時間 API の進行を扱います。
結果はローカル artifact で管理し、系列全体や OEP、外部周辺機能の保証に拡張しません。

## 基礎 API と runtime

実装根拠は [cores/arduino](../cores/arduino/)、[CH32RV](../libraries/CH32RV/src/)。
「現行」は tests/run_hardware.py の case と試験の関数（`<case>::<関数>`）、「旧」の名前は設計の参考で、現在の収集・CI の保証には含めません。

| 契約 | 実装 | 現行の試験 / 旧検証の参考 | 不足する保証 / 必要設備 |
|---|---|---|---|
| startup、RAM 初期化、constructor | あり | あり・限定: startup/crt0_probe、build/startup | ABI/系列差分、失敗時の切分け。P4 と代表 DUT、全対象 build |
| pin encoding、mode、digital I/O | あり | あり・限定: core_api、gpio_probe、unit/board_layer | 未配線 pad/route、package と製品番号の差。外部駆動・観測 |
| EXTI、attach/detach | あり | 現行: signals::test_interrupt（プローブが動かす線で RISING・FALLING・CHANGE の数）、selfcheck::test_gpio（自己検査） | 複数 line、解除後、競合、argument callback の契約 |
| millis/micros/delay | あり | 現行: selfcheck::test_time_rate（ホストの時計と比べる）、runtime（粗い進行） | wrap 境界、clock 条件、独立基準との長時間誤差 |
| pulseIn | あり | 現行: selfsys::test_servo（Servo の幅を pulseIn で読む、自己検査）。旧: core_api の timeout | 既知入力の両極性、境界、timeout 上限。独立 pulse source |
| pulseInLong | あり | なし | pulseIn と分けた幅・timeout 契約。独立 pulse source |
| shiftIn/shiftOut | あり | なし（旧: shiftOut の呼び出しのみ） | bit order、clock edge、既知 payload。peer/波形 |
| heap、String、new/delete | あり | あり・限定: heap_string | OOM、断片化、境界、小 RAM、繰返し。host と小 RAM DUT |
| Print/Stream、printf | あり | あり・限定: print_format、stdio_printf、serial_echo | overload、timeout、partial read/write、境界。host と独立 serial peer |
| math/random 等 | あり | あり・限定: core_api | 再現性、境界値、API 契約。native/host と実機 |
| initVariant/yield/serialEvent | あり | あり・限定: hooks_selftest | dispatch/再入条件と他機能との共存。host と実機 |
| reset/system 情報 | あり | 現行: system::test_reset_reasons（restart で SOFTWARE、プローブの NRST で EXTERNAL）、selfsys::test_identity・test_restart | POWERON と WWDG の要因、系列差 |
| IWDG | 条件付きであり（M030 に無い） | 現行: system::test_watchdog（最後の餌からリセットまでを外から測る、次の起動が WATCHDOG）、selfsys::test_watchdog | 長い時間、ウォッチドッグを付けたままの眠り |
| 休止（`CH32RV.lightSleep` / `deepSleep`）と RTC の時計（`time()` / `settimeofday()`） | AWU の系列（V003・V00x・X033/X035）と RTC の系列（V20x・V205・V30x・L103/M103・H41x）。V103 は外した（試験は n/a） | 現行: system::test_sleep（タイマーと端子で起きる lightSleep、タイマーの deepSleep を外から測る）、selfsys::test_rtc・test_light_sleep。手動（2026-10-27）: 休止の長さのずれ X035 +3 %、V006 −10 % | V103（最初の休止から起きない）、スタンバイの系列の deepSleep の長さ、外部時計との照合 |
| EEPROM API | 出さない（[flash-store](flash-store.ja.md)。内蔵フラッシュは `ch32rv_flash.h`） | — | — |

## 通信

実装根拠: [Wire](../libraries/Wire/src/)、[SoftWire](../libraries/Wire/src/SoftWire.cpp)、
[SPI](../libraries/SPI/src/)、[SoftSPI](../libraries/SPI/src/SoftSPI.cpp)、
[HardwareSerial](../cores/arduino/HardwareSerial.cpp)。

| 契約 | 実装 | 現行の試験 / 旧検証の参考 | 不足する保証 / 必要設備 |
|---|---|---|---|
| UART TX/RX、設定、buffer | あり | 現行: uart::test_uart_contract（独立 UART と往復）・test_uart_output_only・test_uart_formats（9600〜1 Mbaud、8E1・8O1・8N2・7E1・7O1、WireSkein で線の上）・test_uart_receive_errors（parity・frame・break・buffer full・overrun）、periph::test_serial_routes（Serial の route ごと）、selfbus（自己検査） | 全 instance、reset の対象整理 |
| Wire master 正常通信 | あり | 現行: wire::test_wire_output_only（クロックの問い: 20〜400 kHz の SCL）、wire::test_wire_repeated_start（取引の問い: プローブの i2c-target と repeated START・読み書き、20 kHz） | instance/route、buffer 境界、別実装 driver 互換、400 kHz を超えるクロック |
| Wire master NACK/timeout/recovery | あり | 現行: アドレスの NACK のみ（wire::test_wire_output_only、selfbus::test_i2c_nack） | データの NACK、timeout、バスの回復、clock stretch。SDA を握る・NACK を返す peer |
| Wire slave 受信・応答 | あり | 現行: wire::test_wire_slave（プローブの i2c-controller が独立 master、100 kHz。onReceive / onRequest の順と中身、repeated START の register read、0xFF の埋め、32 バイトの頭、別の番地） | callback 文脈、復旧 |
| SoftWire master | あり | 現行: softwire::test_wire_output_only・test_wire_repeated_start（wire と同じ問いを任意の 2 本で） | stretch、NACK、timeout/recovery |
| SoftWire slave | 未実装（今は controller だけ。begin(address) は controller のままで callback は no-op） | なし | 実装と、独立 master（プローブの i2c-controller）での wire.slave と同じ試験 |
| SPI master | あり | 現行: spi::test_spi_output_only・test_spi_contract（mode/order、CS、プローブを相手に往復）、selfbus::test_spi_out / test_spi_in（自己検査） | data/buffer 境界、clock ごとの実観測 |
| SoftSPI master | あり | 現行: softspi（spi と同じ問いを任意の端子で） | 8/16/32-bit/block、MISO 無し、clock floor |
| SPI slave（CH32RVSPISlave）、SoftSPI slave | ハードウェアの SPI（CH32RVSPISlave）。SoftSPI slave は未実装 | 現行: spi::test_spi_slave（プローブが master） | SPI slave の DMA 境界、復旧。SoftSPI slave の実装と同じ試験 |
| SerialDMSeq | あり | 間接利用: 多くの sketch の Console | 損失、buffer、host 不在、再接続、reset の独立契約。対応 probe と別観測経路 |
| SerialSDI | あり | 現行: console-sdi::test_console_sdi | buffer、host 不在、reset。RX を想定しない |
| SerialDMDATA / SerialRTT | あり | 現行: console-dmdata / console-rtt::test_console_two_way | 容量・host 不在・再接続 |

Wire slave は、プローブの i2c-controller を独立 master にして、単一 I2C の DUT でも検証します（旧の同一 DUT 内の
I2C1 master と I2C2 slave の折返しは使いません）。

SPI の既存外部 decode には観測上限があります。高速 capture が取得できたことと、
その速度で payload/mode を検証できたことは別です。
SoftWire/SoftSPI の clock 設定は達成周波数の保証ではなく half-period の下限です。
hardware の厳密な周波数期待値をそのまま適用しません。

## アナログ・timer

実装根拠: [wiring_analog.c](../cores/arduino/wiring_analog.c)、
[wiring_pwm.c](../cores/arduino/wiring_pwm.c)、[CH32RVTimer.h](../cores/arduino/CH32RVTimer.h)、
[Servo](../libraries/Servo/)。

| 契約 | 実装 | 現行の試験 / 旧検証の参考 | 不足する保証 / 必要設備 |
|---|---|---|---|
| analogRead、channel/pad | あり | 現行: periph::test_adc（プローブが動かす線を読む）、selfsys::test_adc_pads（自己検査）、selfcheck::test_vref | 中間電圧、精度（手動の試験。外部の電圧源でカーブを取る）、instance 選択、他 channel 干渉 |
| analogReadResolution | あり | 専用検証なし | 拡張/縮小、範囲/丸め、series の物理分解能との差 |
| analogReference | 互換用 no-op | 専用検証なし | 選択可能な基準電圧ではない。no-op 契約と実 ADC 基準を確認 |
| PWM analogWrite | あり | 現行: signals::test_pwm（1 kHz の 64・128・192、端値、止めた後）、selfcheck::test_pwm（自己検査） | 対象 timer/route、負荷、他 channel との共有条件 |
| analogWriteFrequency / Resolution | あり | 現行: signals::test_pwm（2 kHz と 500 Hz への変更）、analog::test_dac（12 bit） | 丸め、限界、他 channel への影響 |
| DAC analogWrite | DAC のある系列（V30x・V317・V4x7・H41x） | 現行: analog::test_dac（同じ端子を analogRead で読み戻す） | 外部の電圧計/基準 ADC での照合 |
| tone/noTone | あり | 現行: signals::test_tone（440 Hz・2 kHz、noTone 後、30 ms の長さ）、selfsys::test_tone | timer 競合 |
| Servo | あり | 現行: signals::test_servo（50 Hz、544〜2400 µs の幅、detach 後）、selfsys::test_servo | 複数 channel、資源競合 |
| タイマーの入力（CH32RVTimerInput / CH32RVPulseCapture） | あり | 現行: signals::test_timer_input（プローブの PWM の周期と High）、signals::test_capture_after_pwm（analogWrite を止めた後に同じ端子で始められる） | 複数 channel、高い周波数 |
| ADC の DMA（CH32RVAdcStream） | あり | 現行: periph::test_adc_stream（1〜2 本、プローブの方形波の周波数とデューティ） | 3 本以上の並び、長時間 |
| HSADC（CH32RVHsAdc、H41x） | あり | 現行: periph::test_hsadc（20 Msps と 1.25 Msps）。常駐の H417 は HSADC の端子に線が無い（TODO） | 実機の結果 |
| ロジックの取り込み（CH32RVLogicCapture） | あり | 現行: logic | — |
| SyncKit / TaskKit | あり | 現行: synckit::test_synckit_contract・test_taskkit_contract（配線なし、割り込みはタイマー） | — |
| timer 所有権・奪取・解放 | あり | あり・限定: manual/uiapduino_timer_fixture | generation、shared base/channel、stale owner、PWM/tone/Servo 混在、interrupt owner |

ADC の rail テストは channel の大きな異常検出には有用ですが、校正済み精度の保証ではありません。
特定個体の ADC 不具合や実測閾値を系列の一般仕様にしません。
同じ DUT の PWM を同じ DUT の pulseIn で測るだけでは、共通時間基準の誤りを検出できません。

## USB・電源・HID

実装根拠: [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/README.ja.md)、
[CH32RVUsbPd](../libraries/CH32RVUsbPd/)、[UIAPduino HID](uiapduino-hid-upload.ja.md)。

| 契約 | 実装 | 現行の試験 / 旧検証の参考 | 不足する保証 / 必要設備 |
|---|---|---|---|
| Arduino USB 初期化・device API | X033/X035・V20x・V30x・V4x7・H41x・X305/X315 の CDC・HID・MIDI・ベンダー・MSC・CCID・NCM・オーディオ・ビデオ・DFU runtime、WebUSB / MS OS 2.0 | 現行: usb・usb-classes・usb-media（ホスト役は PC）、peer/usb/device（ホスト役は ESP32-S3 / ESP32-P4 / RP2040）。手動確認: 全 example の列挙と動作、CDC 双方向 32 KiB、`ch32rv isp flash --entry auto` での書き込み | suspend / resume、大量転送、controller ごとの結果の区別（coverage の項目はまだ系列単位） |
| Arduino USB host API | X035 R8T6/C8T6/G8U6/G8R6・V20x・V30x の USBFS、V4x7・X305/X315 の第 2 世代 USBHS（CH32RVUsbHost、HID・CDC・MIDI・MSC・ベンダー・CCID・ハブ）。H41x の USBSS は作る（TODO） | 現行: peer/usb/host（usb-peer-host / -classes は ESP32-S3・ESP32-P4・RP2040、-input / -storage は RP2040）。ゲームパッド・システム・生の HID 入力・レポート記述子・GET/SET_REPORT・Feature・SET_PROTOCOL・ロックとレイアウト、CDC の 2 ポートと行の設定・制御線、MSC の LUN と準備できない媒体、MIDI の各メッセージと SysEx、ベンダーの要求 OUT、CCID の状態・Escape・電源オフ・カードの抜き差し、相手の抜き差し。F8U6 で Unsupported を実機確認 | ハブ越し、低速デバイス、複数の装置 |
| USB 対向（CH32RV デバイス ⇔ ESP32-S3 / ESP32-P4 / RP2040 ホスト） | 全デバイスクラス | 現行: peer/usb/device（usb-peer / -classes / -net / -fs / -input は RP2040 と ESP32-S3、-media は ESP32-S3 / ESP32-P4。RP2040 のホストにアイソクロナスが無い） | バルク UVC（EspUsbHost が読まない）、オーディオの全二重（S3 が落とす）、マイクの毎フレーム取得、suspend / resume とリモートウェイクアップ、パケット境界と STALL の解除、相手のリセット後の再通信、コアとの共存 |
| USB controller/speed の系列差 | V103（USB）、V20x（USBFS・USBD）、V30x/V317（USBFS・USBHS 第 1 世代）、V4x7・X305/X315（USBHS 第 2 世代）、H41x（USBFS・USBHS・USBSS） | 現行: 記録の usb をコントローラーごとに書くと、そのコントローラーで usb / usb-peer の case を流す（USB_TEST_CONTROLLER） | vendor driver の存在だけで対応とはしない。controller ごとの結果を coverage で分けること、SS の対向 |
| ADC 内部チャネル（Vrefint・温度・VDD）と L103 のオフセット補正 | V00x 以外の温度センサ持ち・Vrefint 持ちの系列 | 現行: selfcheck::test_vref、selfsys::test_temperature。手動（2026-10-15）: X035・L103・V103・V307 で値を確認 | V20x / V30x の校正値、外部電圧との照合 |
| IWDG（`wdtEnable` の時間） | M030 以外 | 現行: system::test_watchdog（250 ms と 1000 ms を外から測る）。手動（2026-10-15）: 1 秒のリセット | 長い時間 |
| 内蔵フラッシュへの保存（`ch32rv_flash.h`、例 FlashSettings / FlashSettingsTwoBanks） | 全系列 | 現行: tests/instrumented/flash（全関数と断る範囲、ソフトのリセット後の読み戻し、書く間の時間）。手動（2026-10-28、PERSIST-01 M19〜M21）: 12 系列で全関数と 2 つの例、V003・V203・X035 で 2 面の電源断 130 回 | 自動の電源断、寿命、書き込みの経路ごとの保持（約束しない） |
| printf の系統と浮動小数の表示（`ch32rv_format.c`、Floating Point のメニュー） | 全系列 | 現行: unit/test_core_format（PC で 6 通りの設定、`snprintf` と照合）、unit/test_core_parse_float（parseFloat の 3 通りの計算を PC の `strtof` と照合）。手動（2026-10-28）: V003・X035・V307 で Double の `Serial.print` / `String(float)` / `dtostrf` が以前と同じ出力、Float の出力が 3 系列で同じ、`snprintf` の切り詰め、`printf` と `puts` の順番、`parseFloat` の 3 通りの計算が PC と同じビット。手動（2026-10-29）: FPU のない系列の、float の計算を整数で行う表示が、float で行う元の手順と同じ出力（PC で 1 億件余りと float の全ビットパターン、V003・X035・V307 の実機でハッシュ） | 実機の自動試験、newlib の選択肢の出力、`fputs` と混ぜたときの順番 |
| soft-float の部品（`ch32rv_softfloat.c`：float と double の変換、`__clzsi2`、`__ctzsi2`） | FPU のない系列（double は倍精度の FPU のない系列） | 現行: unit/test_core_softfloat（`__clzsi2`、`__ctzsi2`、`__extendsfdf2`、`__truncdfsf2` を PC の結果と、端の値と 251 個に 1 個で照合。LTO の後にできる呼び出しがコアの定義に届くことを clang と lld で確認）。手動（2026-10-29）: 変換と clz を 32 bit の全値で PC と照合、V003・X035 で libgcc のものと変換 130〜450 万件・clz 100 万件がビット単位で一致 | 引き算は compiler-rt のもの（足し算を呼ぶだけ）でコアには無い |
| 時刻の関数（`ch32rv_strftime.c`、`ch32rv_gmtime.c`、`ch32rv_localtime.c`、`ch32rv_tzset.c`） | 全系列 | 現行: unit/test_core_strftime（`strftime` を newlib 4.5 の出力の表と、`tzset` を newlib の結果の表（壊れた TZ を含む 47 件）と、`gmtime_r` を PC のものと 300 万件照合）。手動（2026-10-29）: PC でコンパイルした newlib のソースと、`strftime`・`gmtime_r` 3000 万件、`tzset` 300 万件が全件一致。X035 で `gmtime_r`・`localtime_r`・`strftime` 9000 件の出力のハッシュが newlib と同じ、V003 も同じ | `localtime_r` の自動試験、乱数での newlib との照合（newlib のソースが要るため手動）、`mktime` との組み合わせの実機試験 |
| 端子ごとの AF（V205・X305 / X315 の Serial / Wire / SPI / PWM）、Serial6〜9 | 実装 | 現行: periph::test_serial_routes（Serial の route ごと、ラベルのある線の組）。Wire / SPI / PWM の route はビルドのみ | Wire / SPI / PWM の route の実機 |
| USB PD frame / fixed / PPS 計算 | あり | あり・限定: unit/test_pd_frames、pd_selftest | 境界・不正入力と target 差。native と実機 |
| USB PD sink 交渉 | X033/X035 に実装 | 現行: pd（sink、記録の pd と常駐ベンチの PD の配線が要る。TODO）。旧: pd_sink、pd_vbus | 脱着/再接続、source reset、fault、交渉と VBUS の独立照合。PD source と電圧計/電源制御 |
| USB PD source | X033/X035 に実装 | あり・限定: unit/test_pd_frames_source（Source_Capabilities の PDO、Request の検証）、例のビルド | X035 どうしの対向（Rd 検出と向き、固定/PPS 契約、Reset、PPS 途絶、取り外し、コールバックの失敗）。実際の電圧は範囲外 |
| UIAPduino HID upload recipe | あり（外部 bootloader 経路） | あり・限定: run_hardwareのhid＋独立console上のruntime | HID 書込み→再列挙→sketch 起動、復旧、製品 pin map。UIAPduino と独立 console |

USB データは速さごとの対向（FS は RP2040 と ESP32-S3、HS は ESP32-P4。[テスト計画](../tests/TEST_PLAN.ja.md)の「USB peer 計画」）で、DUT host / peer device と DUT device / peer host の両構成を検証します。
ボードの role は固定せず、ケースごとの profile で指定します。host と device のカバレッジ・実行結果は別々に記録します。
安価な CH32X035 peer に置き換える繰返し検証（まだ case が無い）と、独立実装との相互運用検証は別の coverage 行を持ちます。
USB データを通した結果は、PD の交渉・電圧や bootloader HID の保証ではありません。

## 系列と Arduino エコシステム

実装根拠は board/variant 定義と platform recipe です。旧検証の存在は現行の合否ゲートを意味しません。

| 契約 | 実装 / 定義 | 旧検証の参考（新 suite では未整備） | 不足する保証 |
|---|---|---|---|
| board/part/options と生成物 | あり | unit、build/generated、compile matrix | 正本から生成する契約と build 対象の統合。文書表の表現に依存する status test は再設計 |
| 系列 startup/clock/interrupt/register | あり | build/startup、clock_prescaler、crt0_probe/reg_probe | 代表以外の part/package/route と clock 条件。unit の compiler 依存も明示 |
| examples の API 互換 | あり | compile/examples/profile の matrix | compile と動作を分離。非対応 profile は理由付き対象外 |
| 外部 Arduino library 互換 | TwoWire/SPI 等の互換 API あり | 主に compile、専用 driver 相互運用は限定的 | 代表 driver を固定し、既知 device/peer で読み書きまで検証 |
| package 導入・FQBN 解決 | あり | build/package | 隔離・無上書き条件、依存解決、再導入、実 upload/monitor の契約 |
| upload/reset/monitor | recipe あり | unit/recipe と実機テストでの間接利用 | P4/LinkE/Link/HID を区別した最小 E2E、誤対象防止と再接続 |
| discovery / udev | recipe/rules あり | unit/recipe、build/vendor/udev | Linux の実列挙、権限、identity、再列挙、複数 DUT の選別 |
| UIAPduino 製品 pin/route | 定義あり | unit/compile、manual fixture/pin_map | 製品の公開 pin 契約と汎用 series 契約を分離し HID E2E と接続 |
| probe protocol / TCP 内部 | 外部実装 | trace/tcp_link に混在 | このコアは DUT E2E のみ保持。内部検証は ch32rv/probe 側へ分離 |

Ethernet（Network の NetworkEthernet、現行: eth）と SD（SD の SPI と SD_MMC の SDIO、現行: sd::test_sd_spi・test_sd_sdio）は公開 API があり、試験もあります。
CAN・I2S など silicon にあってコアに無いものは未実装で、作る対象です（coverage.toml では予定の項目。未実装の一覧は TODO）。
分母は silicon の機能です（BLE を除く）。

## 契約 matrix の作り方

各行には contract ID、implementation scope、test definition、target/profile、required fixture capability、
observation method、期待値を結び付けます。
実装範囲は part/package とコードの条件から、テスト範囲は assertion から決めます。
テストファイル名、profile の列挙、console の利用だけで保証を増やしません。

共通契約を系列代表で確認したうえで、instance/route/ABI/製品 pin などの差分契約を追加します。
P4/LinkE/Link/HID は API の分母を増やさず、経路別 integration の軸にします。
未実装、テスト未定義、設備不足、未実行、実行失敗を区別して集計します。
具体的な target/配線 matrix はローカル設備 schema と contract 定義を接続して作成し、
この棚卸しの表を実機 PASS 表として使いません。
