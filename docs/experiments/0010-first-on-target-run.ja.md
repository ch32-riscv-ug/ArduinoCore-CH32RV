# 実験0010: 自作crt0の実機初動作(CH32V003)

実施日: 2026-08-20
対象question: Q-041(probe選択)、Q-044/Q-045(独自書き込みtool)、ADR-0003(own startup)の実機検証
実施環境: WSL2 Linux x86_64 + usbip、arduino-cli 1.3.1、xPack riscv-none-elf-gcc 14.3.0-1、
**実機あり**(CH32V003 16K/2K + WCH-LinkE v2.12)

## 目的

[ADR-0003](../adr/0003-owned-startup-vector-linker.ja.md)のown startupは、EVT startupとの
ELF等価性([tests/startup/](../../tests/startup/))という**静的検査でしか検証されていなかった**。
実機で`crt0_ch32.S` → `setup()`/`loop()`まで到達するかを初めて確認する。

## 手順

RAMを既知パターンで埋めてから起動させ、markerをSWD経由で読み戻す。
「.bssが0だった」が偶然(RAMが元から0)でないことを、**`_ebss`の外側にパターンが残っている**
ことで裏付ける。

1. `arduino-cli compile --fqbn ch32-riscv-ug:ch32v:CH32V003:pnum=ANY`(476 bytes)
2. `minichlink -w Crt0Probe.ino.bin flash -a`
3. `minichlink -a -w <0xDEADBEEF x512> 0x20000000 -b` — **flash後に**RAMを埋めて起動
   (書き込みalgorithm自体がRAMを使うため順序が重要)
4. `minichlink -A -r + 0x20000000 32`

## 結果: 全項目pass

```
20000000: a5 a5 a5 a5 44 91 3a 00 7e d0 70 5e de c0 de c0
20000010: 00 00 00 00 a5 a5 a5 a5 00 00 00 00 ef be ad de
```

| addr | symbol | 読み値 | 意味 |
|---|---|---|---|
| `20000000` | `data_marker` (.data) | `A5A5A5A5` | **`.data`がflashからRAMへcopyされた** |
| `20000004` | `loop_counter` (.bss) | `003A9144`(再読で変化) | **`loop()`が回っている** |
| `20000008` | `setup_marker` | `5E70D07E` | **`setup()`に到達** |
| `2000000c` | `ctor_marker` | `C0DEC0DE` | **`.init_array`が実行された**(C++大域constructor) |
| `20000010` | `bss_seen` | `00000000` | `setup()`時点で`.bss`が0 |
| `20000014` | `data_seen` | `A5A5A5A5` | `setup()`から`.data`が読める |
| `20000018` | `bss_marker` (.bss末尾) | `00000000` | zero fill済み |
| `2000001c` | `_ebss`の外 | **`DEADBEEF`** | **prefillが残存 = 上の0はcrt0が書いた** |

`.init_array`の実行は**EVT startupには無い自前機能**であり、これまで
「`.init_array`が4バイト存在する」という静的checkのみだった。実機で呼ばれることを確認した。

## 副次的に確定したこと

### probeの識別

同一セッションで2台のprobeを差し替え、いずれもuniqueなUSB serialを報告した。

| probe | serial | minichlink判定 |
|---|---|---|
| CH32V103基板のオンボードdebugger | `434A124C5596` | `CH549 version 2.11`(初代WCH-Link) |
| 単体probe | `F90E8F067DFD` | **`LinkE version 2.12`** |

[upload-and-fixture](../upload-and-fixture.ja.md)の「複数WCH-Linkの識別」を実機で確定。
**Q-044/Q-045(独自書き込みtool)の昇格条件は満たされない。**

### 同梱minichlinkの制約(3件)

`~/.arduino15/packages/UIAP/tools/minichlink-2982dfd/1.0.0/minichlink`で観測。

| 制約 | 内容 |
|---|---|
| serial選択不可 | `-l`が`Error: Unknown command l`。上流`ch32fun`にはある |
| 16K一括read不可 | `-r <file> flash 16384`が`Fault reading device`。12288は成功、残りは別addressで読む |
| 起動時reset | 呼び出しのたびにtargetがresetされる(`loop_counter`が読み直しで巻き戻る) |

3点目はHIL harnessでは**むしろ望ましい**(pytest-embeddedはreset起点のlogを期待する)。

### `ANY`メニューの妥当性

silicon側から読めるのは「CH32V003 / flash 16 kB」まで。**packageは判別できない**。
V003の4型番は全て16K/2Kでpackageのみが違うので、
[ADR-0005](../adr/0005-board-structure-and-fqbn.ja.md)の`ANY`既定は実機の観測とも整合する。

## 手順の再現性

元のfirmwareは事前にbackupし、試験後に書き戻してbyte一致を確認した(12288 bytes)。
`minichlink -r`が16Kを一括で読めないため、backupは**分割readで取る**必要がある。

## 未確認

- UART(`Serial.println()`)はまだ。coreにHALが無い
- `delay()`の実時間。現在は`wiring_stub.c`のbusy loopで、SysTickを使っていない
- `PFlags`が`ff-ff-ff-ff`だった意味(V103個体は`25-00-41-0f`)。option byte解釈は未調査
