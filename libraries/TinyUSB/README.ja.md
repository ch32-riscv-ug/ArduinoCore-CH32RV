# TinyUSB

[English](README.md)

チップ自身の USB を USB device として使う library です。CDC の serial port（`SerialUSB`）を提供します。
USB stack は同梱の [TinyUSB](https://github.com/hathach/tinyusb) です（[ADR-0012](../../docs/adr/0012-usb-stack.ja.md)）。

## 対応系列

| 系列 | 状態 |
|---|---|
| CH32X035 | 実機確認済み（WeAct CH32X035F8U6）。列挙、CDC の送受信、1200 bps touch での bootloader 移行 |
| CH32X033 | X035 と同じ USBFS。実機未確認 |
| その他 | build は通るが、`SerialUSB` は開かない（`operator bool` は常に false、書き込みは捨てる） |

## 使い方

```cpp
#include <TinyUSB.h>

void setup() { SerialUSB.begin(); }
void loop()  { SerialUSB.println("hello"); delay(1000); }
```

USB stack は `loop()` の後、`delay()` の中、各 `SerialUSB` 呼び出しの中で処理されます。
これらを通らずに長く止まる sketch では、host から port が止まって見えます。

host が port を開いていないとき、`write()` は待たずに捨てます。

## bootloader への移行（1200 bps touch）

port を 1200 baud で開いて閉じると、USB から切断し、factory USB bootloader（`1a86:55e0`）へ reset します。
そこへ `ch32rv isp flash` で書き込みます。書き込まないと、約 60 秒後に application へ戻ります。

```sh
stty -F /dev/ttyACM0 1200          # 開いて閉じる
ch32rv isp flash sketch.bin --device usb:<bus>-<ports> --wait 5
```

`SerialUSB.setBootTouchEnabled(false)` で無効にできます。sketch から直接移るには
`TinyUSBDevice::enterBootloader()` を呼びます。

application が USB を使えない状態になっても、X033 / X035 は電源投入時に PC17（USB D+）を High にすれば
bootloader に入れます（[upload-routes.ja.md](../../docs/upload-routes.ja.md)）。

## USB ID

既定は pid.codes の試験用 `1209:0001` です。製品では build option で変えます。

```sh
--build-property "build.extra_flags=-DCH32RV_USB_VID=0x1234 -DCH32RV_USB_PID=0x5678"
```

`CH32RV_USB_MANUFACTURER` / `CH32RV_USB_PRODUCT` で文字列も変えられます。serial number はチップの
64 bit unique ID です。

## 同梱 source

`src/` の TinyUSB は固定した upstream の copy です。upstream の revision、license、全 file の SHA-256、
許可した patch は `vendor/tinyusb.lock.toml` が正本です。CH32X035 の USBFS 対応は upstream PR #3703 の
merge commit で、まだ release に入っていないため commit を固定しています。

```sh
uv run --no-project python tools/vendor/vendor_tinyusb.py --check
```

この repository が足した file は `tusb_config.h`、`TinyUSB.h`、`TinyUSB.cpp`、`ch32_tusb_glue.cpp`、
`ch32x035.h` です。`ch32x035.h` は、TinyUSB の register header が読む WCH SDK header の代わりに、
USBFS の register 配置、IRQ 番号、`NVIC_EnableIRQ` だけを定義します。
