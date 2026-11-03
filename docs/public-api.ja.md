# 公開する API

このコアが版をまたいで保つものの一覧です。ここに無いもの（include できても）はコアの内部で、版で変わります。

## スケッチとライブラリが使ってよいもの

| 種類 | 中身 |
|---|---|
| Arduino の API | `Arduino.h`（ArduinoCore-API 1.5.2 と、このコアが足したもの: `HardwareSerial::setPins()` / `setRoute()` など、各 README に書いたもの） |
| 同梱のライブラリ | 各ライブラリの README に書いたクラス、関数、型、定数。ヘッダーは `src/` の直下にあり、名前が `Internal` で終わらないもの（`CH32RVUsbDeviceInternal.h`、`CH32RVUsbHostInternal.h` などは内部） |
| チップの機能 | `CH32RV.h` の `CH32RV` オブジェクト（`restart()`、`resetReason()`、`getUniqueId()`、休止、WDT など） |
| フラッシュ | `ch32rv_flash.h`（`CH32RV_FLASH_STORE()`、`ch32rv_flash_*()`、`CH32RV_FLASH_ERASE_SIZE` などの大きさ。[flash-store.ja.md](flash-store.ja.md)） |
| タイマー | `CH32RVTimer.h`（`ch32rv_timer_*()` と `CH32RVTimer*` の型、`CH32RV_TIMER_ANY` などの定数） |
| 乱数 | `ch32rv_rng.h` の `ch32rv_rng_read()`（ハードウェアの乱数のある系列。`CH32RV_RNG_BASE` で調べる） |
| ピンの対応 | `pins_arduino.h`（Arduino.h から入る）の端子名（`PA0` など）、`CH32RV_PORT_COUNT`、`CH32RV_PIN_BIT()`、`CH32RV_PIN_RESOLVE()`、`CH32RV_PWM_PIN_TO_TIMER()`、`CH32RV_PWM_PIN_TO_CHANNEL()`、各インスタンスの既定の端子（`CH32RV_SPI1_SCK` など） |
| lwIP | Network の README の「lwIP を直接呼ぶとき」に書いた呼び方だけ。lwIP の版と `lwipopts.h` は保たない |

## マクロ

| 種類 | 名前 |
|---|---|
| 版と板 | `ARDUINO_ARCH_CH32RV`、`ARDUINO_<board>`、`CH32RV_SERIES_<series>`、`CH32RV_PART_<part>`、`CH32RV_VERSION`、`CH32RV_VERSION_VAL()` |
| メニューで決まるもの | `CH32RV_RAMSPLIT_RAM_KB`（Flash / RAM Split の RAM の KB）、`F_CPU` |
| 機能を調べる | `CH32RV_HAS_UNIQUE_ID`、`CH32RV_HAS_VIO18`、`CH32RV_RNG_BASE`、`CH32RV_USBPD_BASE`、`CH32RV_SLEEP_UNSUPPORTED` |
| 利用者が決める大きさや時間 | 各 README に書いたもの（`CH32RV_SOFTWIRE_BUFFER_SIZE`、`CH32RV_WIRE_TIMEOUT_US`、`CH32RV_DMSEQ_RX_SIZE`、`CH32RV_DMDATA_RX_SIZE` など）。スケッチより前に定義する（`build_opt.h`、ADR-0007） |
| ライブラリの定数 | 各ライブラリの README に書いたもの（`CH32RV_USB_HID_KEY_*` など） |

ほかの `CH32RV_*`（`boards.txt` が渡すレジスタの番地、クロックの設定、内部の切り替えなど）は内部です。名前も値も、版で変わります。

## 形の決まり

| 種類 | 決まり | 例外 |
|---|---|---|
| コールバック | `CH32RVCallback<R(Args...)>`（`CH32RVCallback.h`）。関数か、小さくキャプチャするラムダ（`[this]` など。ポインター 2 つまで）を受ける。ヒープを使わない。ESP の `std::function` の書き方がそのまま通る | — |
| エラー | 操作は `bool` を返し、理由は `lastError()`（`<名前>Error` の enum class）と `lastErrorName()` で読む | Arduino と ESP の慣習の形はそのまま: Wire、`hostByName()`、`NetworkClientSecure::lastError(buf, size)`（Mbed TLS の番号）、`ccidLastError()`（読み取り機が返した bError）、`CH32RV.lightSleep()` の結果 |
| C の関数 | `ch32rv_<部品>_<動き>()`（`ch32rv_flash_write()`、`ch32rv_timer_try_acquire()`） | Arduino の名前（`deliverEvents()` など） |
| 型と enum | `CH32RV<ライブラリ>...` の名前の enum class（`CH32RVUsbPdSupply::Fixed`） | — |

## 内部のもの（版で変わる）

- レジスタの層: `ch32rv_registers.h`、`ch32rv_gpio.h`、`ch32rv_pins.h`、`ch32rv_route.h`。名前を指して include すれば使えますが、コアも同じレジスタを使っています（[CH32RV の README](../libraries/CH32RV/README.ja.md) の「逃げ道」）。
- `cores/arduino` のほかのヘッダー（`ch32rv_service.h`、`ch32rv_irq.h`、`ch32rv_power.h` など）と、ライブラリの `*Internal.h`、`NetworkCore.h`、`pd_frames.h`、`usbpd_hw.h`。
- 同梱の第三者のコード（lwIP、Mbed TLS、ArduinoCore-API の写し）の版。
