# 実装・テストカバレッジ

[テスト計画](../tests/TEST_PLAN.ja.md)の保証対象を、実装と既存テストの両面から整理します。
これはソースを照合した棚卸しです。この文書の作成時に実機を実行したという意味ではなく、
過去の PASS、全対象での保証、コード行カバレッジを示しません。

## 読み方

実装は「あり」「一部」「未実装」「対象外」を区別します。
「あり」でも silicon/part/route による条件があります。全系列で動くという意味ではありません。
テスト定義は「あり・限定」「間接利用」「ビルドのみ」「なし」を区別します。
「なし」は専用の動作検証を確認できないものです。examples のコンパイルは動作検証に含めません。
不足欄が新しい契約の候補であり、既存テストを残す義務ではありません。

実行カバレッジは別 artifact で contract × target × environment × observation の結果を記録します。
特定系列のテストファイルの存在を、その系列の実機 PASS に変換しません。
リリース状態や、意味のない機能数・テスト数・割合は持ちません。

## 新 workspace の実行検査

tests/ の実行コードは実機不要です。現行の検査・CI・保守ツールは旧 suite に依存しません。
実機用ディレクトリの README は雛形であり、テスト定義としてカバレッジに含めません。

| 入口 | 保証するもの | 保証しないもの |
|---|---|---|
| tests/unit/test_workspace.py | 通常の収集範囲、実機雛形の非実行性、旧 harness との分離 | コア API の動作 |
| tests/unit/test_maintenance_tools.py | profile 同期、version 更新の対象限定、package 入力の自己完結、CI の旧 suite 非依存 | CH32 compile、実機動作 |
| tests/build/test_source_locks.py | ArduinoCore-API/TinyUSB のファイル一覧と lock の hash のオフライン一致 | upstream commit の由来、compile、実機動作 |

## 現行 CI の保証と未整備の検証

現行 CI は新 suite、device-data 再生成、example profile の同期、vendor provenance、隔離 package 導入を実行します。
package 確認は独立ツール内の compile-only source を使い、旧ベンチ sketch を読み込みません。

全型番 compile matrix、全 example × profile の compile、startup 等価性、割り込み表比較、size baseline、
製品 board compile、recipe/udev/chip database の個別契約は、新しい実行入口が未整備です。
旧 job は呼び出しません。新 suite が成功しても、これらが検証されたとは扱いません。
実機契約もすべて新 suite では未整備です。

## 基礎 API と runtime

実装根拠は [cores/arduino](../cores/arduino/)、[CH32RV](../libraries/CH32RV/src/)。
以下の旧ケース名は設計の参考です。現在の収集・CI の保証には含めません。

| 契約 | 実装 | 旧検証の参考（新 suite では未整備） | 不足する保証 / 必要設備 |
|---|---|---|---|
| startup、RAM 初期化、constructor | あり | あり・限定: startup/crt0_probe、build/startup | ABI/系列差分、失敗時の切分け。P4 と代表 DUT、全対象 build |
| pin encoding、mode、digital I/O | あり | あり・限定: core_api、gpio_probe、unit/board_layer | 未配線 pad/route、package と製品番号の差。外部駆動・観測 |
| EXTI、attach/detach | あり | あり・限定: gpio_probe、gpio_loopback | mode、複数 line、解除後、競合、argument callback の契約。外部 edge |
| millis/micros/delay | あり | あり・限定: core_api、periph_probe、reset_probe | wrap 境界、clock 条件、独立基準との長時間誤差。host の境界検査と外部時計 |
| pulseIn | あり | あり・限定: core_api の timeout、gpio_loopback の PWM | 既知入力の両極性、境界、timeout 上限。独立 pulse source |
| pulseInLong | あり | 専用検証なし | pulseIn と分けた幅・timeout 契約。独立 pulse source |
| shiftIn/shiftOut | あり | shiftOut 呼出しはあるがデータ検証なし | bit order、clock edge、既知 payload。peer/波形 |
| heap、String、new/delete | あり | あり・限定: heap_string | OOM、断片化、境界、小 RAM、繰返し。host と小 RAM DUT |
| Print/Stream、printf | あり | あり・限定: print_format、stdio_printf、serial_echo | overload、timeout、partial read/write、境界。host と独立 serial peer |
| math/random 等 | あり | あり・限定: core_api | 再現性、境界値、API 契約。native/host と実機 |
| initVariant/yield/serialEvent | あり | あり・限定: hooks_selftest | dispatch/再入条件と他機能との共存。host と実機 |
| reset/system 情報 | あり | あり・限定: system_selftest、reset_probe、core_api | reset 種別ごとの独立確認、系列差。P4/電源制御 |
| IWDG | 条件付きであり | あり・限定: system_selftest | software reset fallback を IWDG 成功と数えない。実 watchdog/reset reason、独立 timeout 観測 |
| EEPROM API、汎用 sleep/RTC API | 未実装 | なし | 採用範囲の決定が先。silicon や vendor header の存在を公開 API 実装と混同しない |

## 通信

実装根拠: [Wire](../libraries/Wire/src/)、[SoftWire](../libraries/SoftWire/src/)、
[SPI](../libraries/SPI/src/)、[SoftSPI](../libraries/SoftSPI/src/)、
[HardwareSerial](../cores/arduino/HardwareSerial.cpp)。

| 契約 | 実装 | 旧検証の参考（新 suite では未整備） | 不足する保証 / 必要設備 |
|---|---|---|---|
| UART TX/RX、設定、buffer | あり | あり・限定: serial_echo/println、route_selftest、uart_probe | 全 instance/route、baud/format、overflow/reset の対象整理。独立 UART と波形 |
| Wire master 正常通信 | あり | あり・限定: i2c_probe、uiapduino_fixture | instance/route、buffer 境界、別実装 driver 互換。独立 I2C target |
| Wire master NACK/timeout/recovery | あり | あり・限定: wire_selftest、i2c_probe | 障害ごとの status/解放/次回復旧、実測 timeout。stretch/stuck/NACK を制御する peer |
| Wire slave 受信・応答 | あり | あり・限定: manual/i2c_loopback | 同一 DUT の別 I2C 間検証に限定。独立 master、callback 文脈、repeated START、buffer/overread/復旧 |
| SoftWire master | あり | ビルドのみ: examples、専用動作テストなし | 任意 pad、open-drain/pull-up、clock floor、stretch、NACK、timeout/recovery、TwoWire 互換。独立 target/波形 |
| SoftWire slave | 対象外 | なし | controller 専用設計。begin(address) は controller のままで callback は no-op。hardware Wire slave と区別 |
| SPI master | あり | あり・限定: spi_selftest、periph_probe | mode/order、CS、data/buffer 境界、clock ごとの実観測。独立 SPI target/波形 |
| SoftSPI master | あり | ビルドのみ: examples、専用動作テストなし | mode/order、任意 pad、8/16/32-bit/block、MISO 無し、clock floor。独立 target/波形 |
| SPI/SoftSPI slave | 対象外 | なし | 公開 controller API の範囲外。silicon 機能と分ける |
| SerialDMSeq | あり | 間接利用: 多くの sketch の Console | 損失、buffer、host 不在、再接続、reset の独立契約。対応 probe と別観測経路 |
| SerialSDI | あり | ビルドのみ: examples | TX 契約、buffer、host 不在、reset。RX を想定しない |
| SerialDMDATA / SerialRTT | あり | ビルドのみ: examples | 公開する方向のデータ整合性・容量・host 不在・再接続。対応 tool |

Wire slave の同一 DUT 内折返し検証は
I2C1 master と I2C2 slave を配線します。必要な両 peripheral の端子を持つ part/package が必要です。
その条件に合わない DUT の SKIP は、slave が未実装という意味でも、検証成功という意味でもありません。
独立 master を使えば単一 I2C の DUT も slave として検証できます。

SPI の既存外部 decode には観測上限があります。高速 capture が取得できたことと、
その速度で payload/mode を検証できたことは別です。
SoftWire/SoftSPI の clock 設定は達成周波数の保証ではなく half-period の下限です。
hardware の厳密な周波数期待値をそのまま適用しません。

## アナログ・timer

実装根拠: [wiring_analog.c](../cores/arduino/wiring_analog.c)、
[wiring_pwm.c](../cores/arduino/wiring_pwm.c)、[CH32RVTimer.h](../cores/arduino/CH32RVTimer.h)、
[Servo](../libraries/Servo/)。

| 契約 | 実装 | 旧検証の参考（新 suite では未整備） | 不足する保証 / 必要設備 |
|---|---|---|---|
| analogRead、channel/pad | あり | あり・限定: core_api、adc_probe、unit/adc_instances | 中間電圧、精度、instance 選択、他 channel 干渉。既知電圧と独立基準 |
| analogReadResolution | あり | 専用検証なし | 拡張/縮小、範囲/丸め、series の物理分解能との差 |
| analogReference | 互換用 no-op | 専用検証なし | 選択可能な基準電圧ではない。no-op 契約と実 ADC 基準を確認 |
| PWM analogWrite | あり | あり・限定: periph_probe、gpio_loopback、timer fixture | 対象 timer/route、負荷、端値、他 channel との共有条件。外部 waveform |
| analogWriteFrequency / Resolution | あり | 専用の設定契約検証なし | 設定変更、丸め、限界、他 channel への影響。外部 waveform |
| DAC analogWrite | 対応対象であり | 専用動作テストなし | 対応 part/pad、出力コード/分解能、端値。DAC 対応 DUT と電圧計/基準 ADC |
| tone/noTone | あり | あり・限定: tone_selftest、periph_probe | 周波数境界、終了後、timer 競合と時間精度。外部 waveform |
| Servo | あり | あり・限定: servo_selftest（同一 DUT 時間基準） | 独立した周期/幅/jitter、複数 channel、解除、資源競合 |
| timer 所有権・奪取・解放 | あり | あり・限定: manual/uiapduino_timer_fixture | generation、shared base/channel、stale owner、PWM/tone/Servo 混在、interrupt owner |

ADC の rail テストは channel の大きな異常検出には有用ですが、校正済み精度の保証ではありません。
特定個体の ADC 不具合や実測閾値を系列の一般仕様にしません。
同じ DUT の PWM を同じ DUT の pulseIn で測るだけでは、共通時間基準の誤りを検出できません。

## USB・電源・HID

実装根拠: [TinyUSB](../libraries/TinyUSB/README.ja.md)、
[USBPD](../libraries/USBPD/)、[UIAPduino HID](uiapduino-hid-upload.ja.md)。

| 契約 | 実装 | 旧検証の参考（新 suite では未整備） | 不足する保証 / 必要設備 |
|---|---|---|---|
| TinyUSB source の固定 | あり | 現行: tests/build/test_source_locks.py、tools/vendor/vendor_tinyusb.py | 動作保証ではない。source/patch/lock の整合のみ |
| Arduino USB 初期化・device API | 未実装 | なし | core/IRQ/clock/descriptor/lifecycle の結線、CH32X035 device と独立 host |
| Arduino USB host API | 未実装 | なし | core/IRQ/clock/driver/lifecycle の結線、CH32X035 host と独立 device |
| CDC/HID 等の sketch USB class | 未実装 | なし | host 側 driver と device 側 class の control/data/error/復旧契約を別々に検証。対向機と role 別 profile |
| USB controller/speed の系列差 | Arduino 統合は未実装 | source 固定のみ | vendor driver の存在だけで対応とはしない。実装採用対象ごとの build と実機 |
| USB PD frame / fixed / PPS 計算 | あり | あり・限定: unit/test_pd_frames、pd_selftest | 境界・不正入力と target 差。native と実機 |
| USB PD sink 交渉 | X033/X035 に実装 | あり・限定: pd_sink、pd_vbus | 脱着/再接続、source reset、fault、交渉と VBUS の独立照合。PD source と電圧計/電源制御 |
| USB PD source | 未実装 | なし | sink の成功で source 対応とはしない。採用範囲の決定が先 |
| UIAPduino HID upload recipe | あり（外部 bootloader 経路） | 定義・build 検査あり、専用 upload E2E なし | HID 書込み→再列挙→sketch 起動、復旧、製品 pin map。UIAPduino と独立 console |

USB データは CH32X035–ESP32S3 を基準に、DUT host / peer device と DUT device / peer host の両構成を検証します。
ボードの role は固定せず、ケースごとの profile で指定します。host と device のカバレッジ・実行結果は別々に記録します。
安価な CH32X035 peer に置き換える繰返し検証と、独立実装との相互運用検証は別の coverage 行を持ちます。
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

CAN/Ethernet/I2S/SDIO 等は silicon に存在しても、この表では公開 Arduino API があるとは扱いません。
この計画は公開する API と採用する拡張を対象とし、silicon の全機能を分母にしません。

## 契約 matrix の作り方

各行には contract ID、implementation scope、test definition、target/profile、required fixture capability、
observation method、期待値を結び付けます。
実装範囲は part/package とコードの条件から、テスト範囲は assertion から決めます。
テストファイル名、profile の列挙、console の利用だけで保証を増やしません。

共通契約を系列代表で確認したうえで、instance/route/ABI/製品 pin などの差分契約を追加します。
P4/LinkE/Link/HID は API の分母を増やさず、経路別 integration の軸にします。
未実装、対象外、テスト未定義、設備不足、未実行、実行失敗を区別して集計します。
具体的な target/配線 matrix はローカル設備 schema と contract 定義を接続して作成し、
この棚卸しの表を実機 PASS 表として使いません。
