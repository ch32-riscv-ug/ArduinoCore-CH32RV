# アーキテクチャ

## 依存方向

```text
sketch / external library
        ↓
ArduinoCore-API + bundled library APIs
        ↓
core implementation / internal CH32RV HAL
        ↓
generated variant data and owned register definitions
        ↓
CH32 hardware
```

上の層から下の層だけへ依存します。series 固有の条件を Arduino API や sketch へ漏らさず、
差分は variant、生成 header、内部 HAL で吸収します。

## Arduino API 境界

`cores/arduino/api/` は ArduinoCore-API 1.5.2 の固定コピーです。通常の Arduino API はこの型を使い、
CH32 固有 API は同梱ライブラリへ置きます。`api/` へのローカル変更は lock file に記録された patch 以外
認めません。

この境界を固定する理由は、外部ライブラリが依存する Arduino の型と、本コアの低水準実装を分離するためです。

## コアが所有するもの

`cores/arduino/` は次を所有します。

- startup、CRT、vector table、linker の共通部分
- GPIO、割り込み、時間、ADC、PWM、tone、UART の Arduino API 実装
- clock、pin、route、timer、register access の内部実装
- newlib syscall と C++ runtime の最小補助

WCH EVT の source/header は取り込みません。必要な register 定義はこのプロジェクトが所有し、
デバイスごとの事実は `ch32-device-data` から生成します。これにより、EVT の再配布条件や directory 構造を
コアの公開 API へ持ち込みません。

## variant と board

Generic board は series 単位です。variant は series の pad、peripheral route、能力、既定 pin を表し、
Part Number メニューが型番ごとの memory layout を選びます。製品 board は同じ series の実装を使い、
基板固有の印刷番号、LED、bootloader、upload protocol だけを追加します。

詳細は [board-layer-rules.ja.md](board-layer-rules.ja.md) と
[ADR-0005](adr/0005-board-structure-and-fqbn.ja.md) を参照してください。

## 同梱ライブラリ

同梱するのは次のいずれかです。

- Arduino で事実上標準の API (`Wire`, `SPI`, `Servo`)
- コア固有機能を公開するもの (`CH32RV`, debug serial classes, `USBPD`)
- hardware resource の調停をコアと共有するため外部化できないもの

一般的なデバイスドライバは同梱しません。examples は Arduino IDE に表示するためライブラリ配下へ置き、
core API の examples は `libraries/CH32RV/` に置きます。

## tool と生成データ

- `tools/generate/` は固定した `ch32-device-data` から `boards.txt` と `variants/` を生成する
- `tools/index/` は release archive と package index を生成する
- `ch32rv` は upload、port discovery、monitor を担当する
- xPack GCC は compile/link を担当する

通常の sketch build は生成器やネットワークを必要としません。生成元の更新は保守作業であり、差分を review して
生成物と lock file を同じ commit に入れます。
