# UIAPduino Pro Micro CH32V003 V1.4 の HID 書き込み

対象 board の FQBN は `ch32-riscv-ug:ch32rv:UIAPDUINO_V003_V14`、bootloader の USB ID は
`1209:b803` です。

## 通常の書き込み

1. RESET を押したまま USB へ接続し、HID bootloader を起動する
2. Arduino IDE で UIAPduino board と `1209:b803` の port を選ぶ
3. Upload を実行する

CLI から binary を直接書く場合:

```sh
ch32rv boot hid flash MySketch.ino.bin --usb-id 1209:b803
```

HID route は ELF ではなく Arduino build が生成した `.bin` を使います。CH32V003 user flash の範囲を超える image は
拒否され、書き込み後は read-back verify を行います。成功後は application が起動するため、HID device が USB から
消えるのが正常です。

## Application 実行中の USB 表示

この board は USB D- に固定 pull-up を持ちます。USB stack を使わない application が D- を解放すると、host には
descriptor に応答しない device として見えます。専用 variant の `initVariant()` は PD4 を output low にして切断状態を
作ります。

USB library は開始時に PD3 / PD4 を自分で設定します。sketch が独自の `initVariant()` を定義した場合は、その sketch が
PD4 の初期状態にも責任を持ちます。

## Bootloader に戻れない場合

bootloader は外部 reset の履歴 (`RCC_RSTSCKR.PINRSTF`) と boot 条件を確認します。application が reset flags を消した後は、
software reset だけで bootloader に留まれません。RESET pin を使ってから boot entry を行います。

OEP probe と SWIO/RESET を配線した fixture では、`oep_client.v1.uiapduino` の helper を使えます。

```python
from oep_client.v1 import link, riscv, core, uiapduino

host = link.open_host("<probe-port>")
host.open()
wire = riscv.Wire(host, "oep.wire.swio")
gpio = core.find(host, "oep.fixture.gpio")

uiapduino.enter_bootloader(host, wire, gpio, <reset-gpio>)
```

SWIO は UIAPduino の PD1、RESET は PD7、logic level は 3.3 V です。GND を共通にし、別 probe が同じ SWIO を保持して
いないことを確認します。

## 復旧時の原則

- HID が見えないだけで bootloader 領域を上書きしない
- まず cable、USB enumeration、RESET、SWIO 応答を切り分ける
- user flash の復旧と bootloader の復旧を別操作として扱う
- read protection の解除は user flash を消去し得るため、backup なしで実行しない
- failure capture には firmware 内容が含まれるため、共有前に内容を確認する

HID transfer が途中で失敗した場合は、bootloader へ入り直して同じ command を再実行できます。page ごとの事前照合により、
一致している page は再書き込みされません。

```sh
ch32rv --capture hid-failure.ndjson \
  boot hid flash MySketch.ino.bin --usb-id 1209:b803
```

純正 UIAP core 互換の `pinV32_DisconnectDebug(PD_1)` も専用 variant が提供します。この呼び出しは reset まで SWIO を
無効化するため、PD1 を GPIO として使う application だけが明示的に呼びます。
