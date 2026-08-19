# 実機smoke test

実機を1台つないで「そのboardで`Serial.println()`が出るか」だけを見るbring-up用のrunnerです。
compileも書き込みも**利用者と同じ経路**(`arduino-cli upload --programmer wch-link` → probe-rs)を
通るので、passは「コードが動く」ではなく「出荷経路が動く」ことを意味します。

## 使い方

```bash
export CH32_GCC_BIN=~/.arduino15/internal/ch32-riscv-ug_xpack-riscv-none-elf-gcc_14.3.0-1_*/bin
# probe-rsはBoard Manager導入分を自動で探します。開発ツリーで別の場所を使うなら:
# export CH32_PROBE_RS=/path/to/probe-rs-tools-<target>

uv run tests/hardware/smoke.py --board CH32X035
```

boardが既定と違うUSARTを配線している場合は`--serial <n>`で切り替えます
(どれが繋がっているかは`uart_scan.py`が特定します):

```bash
uv run tests/hardware/uart_scan.py --board CH32V203   # -> WIRED: U1-PA9
uv run tests/hardware/smoke.py     --board CH32V203 --serial 1
```

probeとそのUARTブリッジは**USB VID:PIDから自動検出**します。このベンチはboardごと
差し替えるので`/dev/ttyACM*`は固定できません。複数繋がっているときは`--probe <USB serial>`、
明示したいときは`--port`。WCH-LinkEは`ff`+CDC×2構成なので、
**書き込みとSerial受信が1本のケーブルで済みます**。

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
| 2026-08-20 | CH32V203(64K/20K) | WCH-LinkE v2.12 | **pass**(`--serial 1`、boardの配線がUSART1/PA9) |
| 2026-08-20 | CH32X035(62K/20K) | WCH-LinkE v2.12 | コアは動作、**UART未接続**(6 route全滅、旧コア・WCH公式でも出力なし) |
| 2026-08-20 | CH32V203C8T6 | WCH-LinkE v2.12 | **pass**。`tests/sketches/basic/core_api`も13 check全pass |

書き込み前に`probe-rs info`でチップを読み、`--board`と食い違ったら止まります
(ベンチはboardを差し替えるため)。意図的に上書きするなら`--force`。
