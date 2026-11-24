# 公開する API

このコアが版をまたいで保つものの一覧です。内部のものは置き場所で分かります: コアの `cores/arduino/internal/` と、各ライブラリの `src/internal/` です。そこにあるもの（include できても）は、版で変わります。

## スケッチとライブラリが使ってよいもの

| 種類 | 中身 |
|---|---|
| Arduino の API | `Arduino.h`（ArduinoCore-API 1.5.2 と、このコアが足したもの: `HardwareSerial::setPins()` / `setRoute()`、ESP32 の `onReceiveError()`・`analogReadMilliVolts()`・`rgbLedWrite()` / `rgbLedWriteOrdered()`（`neopixelWrite()` は非推奨の別名）、チェーンを 1 つのフレームで送る `ch32rv_rgb_led_write()`、それが ARGB のブロックで送る端子かを答える `ch32rv_rgb_led_is_hardware()`など、各 README に書いたもの） |
| 同梱のライブラリ | 各ライブラリの README に書いたクラス、関数、型、定数。ヘッダーは `src/` の直下のもの（`src/internal/` は内部） |
| チップの機能 | `CH32RV.h` の `CH32RV` オブジェクト（`restart()`、`resetReason()`、`getUniqueId()`、休止、WDT など） |
| フラッシュ | `ch32rv_flash.h`（`CH32RV_FLASH_STORE()`、`ch32rv_flash_*()`、`CH32RV_FLASH_ERASE_SIZE` などの大きさ。[flash-store.ja.md](flash-store.ja.md)） |
| タイマー | `CH32RVTimer.h`（`ch32rv_timer_*()` と `CH32RVTimer*` の型、`CH32RV_TIMER_ANY` などの定数）、`CH32RVHwTimer.h`（arduino-esp32 3.x の `timerBegin()` などの API。Arduino.h から入る） |
| 2 つのコア | `ch32rv_multicore.h`（CH32H41x: `ch32rv_hsem_*()`、`ch32rv_ipc_*()`。`CH32RV.hsem*()` / `ipc*()` と同じ） |
| DMA のバッファ | `ch32rv_dma_mem.h` の `CH32RV_DMA_MEM`（Arduino.h から入る。CH32H41x の V5F で DMA が読み書きするバッファを共有 SRAM に置く。ほかの系列では何もしない） |
| コールバックの型 | `CH32RVCallback.h` の `CH32RVCallback<R(Args...)>`（下の「形の決まり」） |
| 乱数 | `Arduino.h` の `ch32rv_random()` / `ch32rv_fill_random()`（arduino-esp32 の `esp_random()` / `esp_fill_random()`。回路の無い系列では暗号に使えないソフトの値）と、`ch32rv_rng.h` の `ch32rv_rng_read()`（ハードウェアの乱数のある系列。`CH32RV_RNG_BASE` で調べる。失敗を false で返す） |
| CRC | `ch32rv_crc.h` の `ch32rv_crc32()` / `ch32rv_crc32_update()`、`CH32RV_CRC32_INIT`（CRC-32/MPEG-2。`CH32RV.crc32()` と同じ） |
| 割り込みの優先度 | `ch32rv_priority.h` の `ch32rv_irq_set_priority()` / `ch32rv_irq_get_priority()`、`ch32rv_pin_irqn()`、`CH32RV_IRQ_PRIORITY()`、`CH32RV_IRQ_PREEMPT_BITS` / `_LEVELS`、`CH32RV_IRQ_PRIORITY_CORE`、`CH32RV_IRQN_CORE_SYSTICK`、割り込みの番号 `CH32RV_IRQN_<名前>`（EVT の名前。コアの割り込みとの約束は CH32RV の README） |
| ピンの対応 | `pins_arduino.h`（Arduino.h から入る）の端子名（`PA0` など）、`CH32RV_PORT_COUNT`、`CH32RV_PIN_BIT()`、`CH32RV_PIN_RESOLVE()`、`CH32RV_PWM_PIN_TO_TIMER()`、`CH32RV_PWM_PIN_TO_CHANNEL()`、各インスタンスの既定の端子（`CH32RV_SPI1_SCK` など） |
| lwIP | Network の README の「lwIP を直接呼ぶとき」に書いた呼び方だけ。lwIP の版と `lwipopts.h` は保たない |

## マクロ

| 種類 | 名前 |
|---|---|
| 版と板 | `ARDUINO_ARCH_CH32RV`、`ARDUINO_<board>`、`CH32RV_SERIES_<series>`、`CH32RV_PART_<part>`、`CH32RV_FLASH_SIZE` / `CH32RV_RAM_SIZE`（選んだ品でスケッチが使える flash と RAM のバイト数。ANY はその系列でいちばん小さい品、V30x は RAM の分け方のメニューのとおり）、`CH32RV_VERSION`、`CH32RV_VERSION_VAL()` |
| メニューで決まるもの | `CH32RV_RAMSPLIT_RAM_KB`（Flash / RAM Split の RAM の KB）、`F_CPU`（スケッチを動かすコアのクロック）、`CH32RV_HCLK_HZ`（周辺の回路とタイマーが数える HCLK。CH32H41x の V5F では CPU Clock の項目の V3F の数（既定で F_CPU 400 MHz に対して 100 MHz）、ほかでは F_CPU と同じ）、`CH32RV_RUN_V5F`（CH32H41x の CPU Core が V5F のとき 1） |
| 機能を調べる | `CH32RV_HAS_UNIQUE_ID`、`CH32RV_HAS_VIO18`、`CH32RV_HAS_CRC_HARDWARE`、`CH32RV_RNG_BASE`、`CH32RV_USBPD_BASE`（休止できない系列は `CH32RV.lightSleep()` が `CH32RVSleepResult::Unsupported` を返す。マクロではない） |
| 利用者が決める大きさや時間 | 各 README に書いたもの（`CH32RV_WIRE_BUFFER_SIZE`、`CH32RV_WIRE_TIMEOUT_US`、`CH32RV_DMSEQ_RX_SIZE`、`CH32RV_DMDATA_RX_SIZE` など）。スケッチより前に定義する（`build_opt.h`、ADR-0007） |
| ライブラリの定数 | 各ライブラリの README に書いたもの（`CH32RV_USB_HID_KEY_*` など） |

ほかの `CH32RV_*`（`boards.txt` が渡すレジスタの番地、クロックの設定、内部の切り替えなど）は内部です。名前も値も、版で変わります。

## 形の決まり

| 種類 | 決まり | 例外 |
|---|---|---|
| コールバック | `CH32RVCallback<R(Args...)>`（`CH32RVCallback.h`）。関数か、小さくキャプチャするラムダ（`[this]` など。ポインター 2 つまで）を受ける。ヒープを使わない。ESP の `std::function` の書き方がそのまま通る。「関数と `void *`」の組は使わない（ラムダのキャプチャで渡す） | C のヘッダー（`CH32RVTimer.h`、`ch32rv_multicore.h`、`ch32rv_irq_attach()`）は関数ポインターと `void *`。Arduino と ESP の形はそのまま: `attachInterrupt()` / `attachInterruptArg()`、Wire の `onReceive()` / `onRequest()`、Ticker、`timerAttachInterrupt()`。関数ポインターを受けるもの（`CH32RV` の `setAlarm()`・`attachExtiLine()`・`wwdgEnable()`・`setLowVoltageDetect()`・`onIpcReceive()`、CH32RVLowPowerTimer の `onPeriod()` / `onCompare()`、CH32RVUsbDeviceMsc の `onRead()` / `onWrite()` / `onStartStop()`）は、あとで CH32RVCallback に広げても書き方は変わらない |
| 名前 | `on<出来事>(cb)` はコールバックを設定する。状態を問うものは `is<状態>()`（`isOnV3F()`、`isAttached()`） | Arduino と ESP の名前 |
| エラー | 操作は `bool` を返し、理由は `lastError()`（`<名前>Error` の enum class）と `lastErrorName()` で読む | Arduino と ESP の慣習の形はそのまま: Wire、`hostByName()`、`NetworkClientSecure::lastError(buf, size)`（Mbed TLS の番号）、`ccidLastError()`（読み取り機が返した bError）、`CH32RV.lightSleep()` の結果 |
| C の関数 | `ch32rv_<部品>_<動き>()`（`ch32rv_flash_write()`、`ch32rv_timer_try_acquire()`） | Arduino の名前（`deliverEvents()` など） |
| 型と enum | `CH32RV<ライブラリかクラス>...` の名前の enum class、値は大文字で始める（`CH32RVUsbPdSupply::Fixed`、`CH32RVResetReason::PowerOn`、`CH32RVPwmTimerAlign::Center`） | C のヘッダーは C の enum（`CH32RVTimer.h` の `CH32RV_TIMER_KIND_*`）。Arduino と ESP の形はそのまま: `PinStatus`、HardwareSerial の `UART_MODE_*`・`hardwareSerial_error_t`、I2S の `I2S_MODE_*` など、EspUsbHost に合わせた `CH32RV_USB_HOST_*` の定数 |
| I2C と SPI のクラス | `TwoWire`（Wire ライブラリ）と `SPIClass`（SPI ライブラリ。ArduinoCore-API の typedef は vendor の patch で外した）は、ハードウェアのバスとソフトのバスの共通の型（Serial の `HardwareSerial` と同じ役）。ハードウェアは `CH32RVTwoWire` / `CH32RVSPIClass`、ソフトは `SoftWire` / `SoftSPI`。どの周辺を使うかは `begin()` の端子で決まり、ソフトかハードかはクラスで決まる（自動では切り替えない）。ボードメニューの I2Cn / SPIn の Software で、その名前（`Wire`、`SPI` …）をソフトにできる | `Wire` / `SPI` の型は具体的なクラスなので、`extern TwoWire Wire;`、`extern SPIClass SPI;`、ESP32 の `SPIClass spi(bus)` の書き方は通らない |

## 内部のもの（版で変わる）

- `cores/arduino/internal/` のすべて。レジスタの層（`internal/ch32rv_registers.h`、`internal/ch32rv_gpio.h`、`internal/ch32rv_pins.h`、`internal/ch32rv_route.h`）も、名前を指して include すれば使えますが、コアも同じレジスタを使っています（[CH32RV の README](../libraries/CH32RV/README.ja.md) の「逃げ道」）。生成物（`clock_init_*`、`irqn_*`、`exti_*`、`vectors_*`）もここにあります。
- 各ライブラリの `src/internal/`（`CH32RVUsbDeviceInternal.h`、`CH32RVUsbDcd.h`、`NetworkCore.h`、`pd_frames.h`、`usbpd_hw.h` など）。
- 同梱の第三者のコード（lwIP、Mbed TLS、ArduinoCore-API の写し）の版。
