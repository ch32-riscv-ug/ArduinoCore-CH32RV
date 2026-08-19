# 実機smoke test

実機を1台つないで「そのboardで`Serial.println()`が出るか」だけを見るbring-up用のrunnerです。
`arduino-cli upload`はまだ経路が無い(Q-040/Q-044)ので、minichlinkを直接呼びます。
programmer定義が入ったら、同じ確認をpytestの[profile](../sketches/basic/serial_println/sketch.yaml)経由へ移します。

## 使い方

```bash
export CH32_GCC_BIN=~/.arduino15/internal/ch32-riscv-ug_xpack-riscv-none-elf-gcc_14.3.0-1_*/bin
export CH32_MINICHLINK=~/.arduino15/packages/UIAP/tools/minichlink-*/1.0.0/minichlink

python3 tests/hardware/smoke.py --board CH32X035 --port /dev/ttyACM4
```

`--port`はprobe内蔵UARTブリッジのCDCポートです(WCH-LinkEは`ff`+CDC×2構成なので、
**書き込みとSerial受信が1本のケーブルで済みます**)。
probeが複数ある場合は`--probe <USB serial>`で選べますが、
**同梱の`minichlink-2982dfd`は`-l`を持たない**ので上流buildが要ります([実験0011](../../docs/experiments/0011-milestone1-serial-on-v003.ja.md))。

内容は[Milestone 1受け入れsketch](../sketches/basic/serial_println/serial_println.ino)と同一で、
`hello from ch32` / `int=42` / `hex=BEEF`の3行が出れば pass です。

## 結線

runnerは実行前に対象boardのTX/RXを表示します。**TX → probeのRX、RX → probeのTX**、GND共通。

`Serial`がどのUSARTになるかは**series単位**で決まります。boardの既定メニューは`ANY`
([ADR-0005](../../docs/adr/0005-board-structure-and-fqbn.ja.md))なので、
そのseriesの全型番で同じpadに出るrouteを生成器が選びます
(`tools/generate/generate.py`の`choose_uarts`)。

| board | Serial | TX | RX | route | 対応型番 |
|---|---|---|---|---|---|
| `CH32L103` | USART1 | `PB6` | `PB7` | remap-1 | 全型番 |
| `CH32M007` | USART1 | `PD5` | `PD6` | default | 全型番 |
| `CH32M030` | USART1 | `PC1` | `PC0` | default | 3 / 5型番 |
| `CH32M103` | USART1 | `PB6` | `PB7` | remap-1 | 全型番 |
| `CH32V002` | USART1 | `PD5` | `PD6` | default | 4 / 5型番 |
| `CH32V003` | USART1 | `PD5` | `PD6` | default | 全型番 |
| `CH32V004` | USART1 | `PD5` | `PD6` | default | 全型番 |
| `CH32V005` | USART1 | `PD5` | `PD6` | default | 3 / 4型番 |
| `CH32V006` | USART1 | `PD5` | `PD6` | default | 6 / 7型番 |
| `CH32V007` | USART1 | `PD5` | `PD6` | default | 全型番 |
| `CH32V203` | USART2 | `PA2` | `PA3` | default | 全型番 |
| `CH32V205` | USART1 | `PB6` | `PB7` | af-2 | 全型番 |
| `CH32V208` | USART1 | `PB6` | `PB7` | remap-1 | 全型番 |
| `CH32V303` | USART1 | `PA9` | `PA10` | default | 全型番 |
| `CH32V305` | USART1 | `PB6` | `PB7` | remap-1 | 全型番 |
| `CH32V307` | USART1 | `PA9` | `PA10` | default | 全型番 |
| `CH32V317` | USART1 | `PA9` | `PA10` | default | 全型番 |
| `CH32V407` | USART1 | `PA9` | `PA10` | default | 全型番 |
| `CH32V467` | USART1 | `PA9` | `PA10` | default | 全型番 |
| `CH32X033` | USART2 | `PA2` | `PA3` | default | 全型番 |
| `CH32X035` | USART2 | `PA2` | `PA3` | default | 全型番 |
| `CH32X305` | USART1 | `PC4` | `PA12` | af-1 | 全型番 |
| `CH32X315` | USART2 | `PD6` | `PD7` | af-1 | 全型番 |

- **route**が`default`以外のboardは、`begin()`がAFIO `PCFR1`のremapフィールドを書きます
  (値はdevice-dataの`remap_fields.csv`から生成)
- `af-N`の3 series(V205/X305/X315)は**per-pin alternate-function方式でremapとは別機構**です。
  コアはまだ設定しないので**未検証**。いずれも`[compile only]`のboardです
- 「全型番」でないboardは、小さいpackageでそのpadが出ていません。
  `ANY`で焼いた場合そのpackageではSerialが出ませんが、
  実在しないpadへの書き込み自体は無害です([ADR-0010](../../docs/adr/0010-pin-numbering.ja.md))

## 実機で確認済み

| 日付 | board | probe | 結果 |
|---|---|---|---|
| 2026-08-20 | CH32V003(16K/2K) | WCH-LinkE v2.12 | **pass**(TX/RX両方向) |
