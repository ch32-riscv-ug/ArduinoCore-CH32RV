# oep_i2c_trace — X035 の I2C master transaction を線上で見る（worklist B）

`i2c_probe_write` sketch を OEP probe 経由で X035 に書き、fixture.uart で `WRITE <route> <hz> <addr> <hex>` を送り、
同じ plan に入れた P4 の I2C target（`p4.i2c-target`、address 0x42）と `fixture.capture`（SCL/SDA を 1 MHz sample）で
線上の START / byte / ACK / STOP を decode して、`Wire.endTransmission()` の戻り値と並べる。

```
uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py                      # 100 kHz と 10 kHz、0x42（target）と 0x43（無応答）
uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --raw-dir /tmp/raw   # capture の生 sample も残す
```

配線（2026-09-22 の fixture）: X035 route 2 = PC16 (SCL) / PC17 (SDA) → P4 GPIO52 / GPIO50。console は USART4 PB0/PB1 → P4 GPIO12/6。
route 4（PC17 SCL / PC16 SDA）は同じ 2 本の役割交換。route 3（PC19/PC18）は probe の SWD 線と衝突して P4 が落ちるので使えない。

## 2026-09-22 の結論: X035 の USB pad は `USB_PHY_V33` が立っていると open-drain を release できない

原因は X035 側。PC16/PC17 は USB PHY の pad で、`AFIO_CTLR.USB_PHY_V33`（bit 6、reset 値 0x45 に含まれる）が 1 の間は
GPIO open-drain でも AF open-drain でも「release」中に外部から low に引けない（X035 を halt して P4 に GPIO50 を low 駆動させ、
X035 の INDR を読んで確認: push-pull HIGH と同じ挙動。INPUT_PULLUP のときだけ引ける）。bit 6 を落とすと P4 が引け、
hardware I2C の route 2 は `S 84A 10A 11A 12A 13A P` / rc=0 になり、SDA の立上りも 4 µs → 0.2 µs になった。
core は `ch32_gpio_set_config()` で PC16/PC17 を出力系に設定する時にこの bit を落とす（errata `x035-usb-pads-open-drain`）。

| 条件（修正後） | Wire rc | 線上 | target 受信 |
|---|---:|---|---|
| route 2、100 kHz、0x42 | 0 | `S 84A 10A 11A 12A 13A P` | 一致 |
| route 2、10 kHz、0x42 | 0 | `S 84A 40A 41A 42A 43A P` | 一致 |
| route 2、0x43（無応答） | 2 | `S 86N P` | — |

切り分けの経過（誤った容疑を含めて残す）: SDA hold（0.2〜0.4 µs）と立上り 4 µs を疑ったが、X035 の bit-bang（ACK slot だけ INPUT_PULLUP）で ACK、
GPIO OD のまま release する高速 bit-bang では hold 0.2〜4.8 µs 全域で NACK → 「release が release でない」に到達した。
peer P4 の IDF master に `sda_hold` を書いても transaction ごとに書き戻されて波形は変わらない（E1 は無効）。

## 修正前の記録（worklist B「X035 から P4 0x42 へ write すると NACK」）

| 条件 | Wire rc | 線上 |
|---|---:|---|
| route 2、100 kHz、0x42（P4 target 待機中） | 2 | `S 84N P` — address byte は正しく 0x84、**9 clock 目で slave が SDA を引かず NACK** |
| route 2、10 kHz、0x42 | 2 | `S 84N P`、SCL 周期 100 µs |
| route 2、100 kHz、0x43（無応答） | 2 | `S 86N P` |
| route 4（線の役割交換）、slave も交換 | 2 | `S 84N P` |
| 参照: peer P4（IDF master）→ 同じ P4 slave、GPIO32/33 | 0 | `S 84A 10A 11A 12A 13A P` |

X035 側の波形（5 MHz sample、200 ns 分解能）: START hold 5.2 µs、SCL 5/5 µs、**SDA の変化は SCL 立下りの 200〜400 ns 後**（spec の
t_HD;DAT ≥ 0 は満たす）、SDA/SCL の立上りは約 4 µs（外部 pull-up 無し、X035 の AF-OD 時の内蔵 pull-up だけ。standard mode の 1 µs を超える）。
参照の IDF master は SDA hold 1.2〜1.6 µs、立上り約 1 µs。

P4 slave 側（`p4.i2c-target read_hw`）: address は受けている（`det_start`、`byte_trans_done`）、`slave_addr_unmatch` は立たない、
RX FIFO は空。address が一致しているのに ACK を出していない形。

**除外できたもの**: X035 の address bit 列（decode で 0x84）、P4 pin（GPIO50/52 は X035 側 INDR で駆動確認済み、役割交換でも同じ）、
slave 生成時の bus が low だった順序、slave の SDA filter 遅延（thres 7）、P4 側 pull-up 追加（立上りは変わらず 4 µs）。
（当時の残る容疑は SDA hold と立上りだったが、上記のとおり原因は `USB_PHY_V33` だった。）

**注意**: `p4.i2c-target` の fixed-rx は v1 driver の制約で「address 後に終わった transaction」でも rx_done が発火し、長さ情報が無いので
古い buffer 内容を frame として返す。frame の存在を ACK の証拠にしないこと（このため 4 case とも `target rx=10111213` と出る）。

## 副産物: UART を再 lease すると DUT の行バッファにゴミが残る（同日、両側で修正）

probe が `Serial1.end()` → `begin()` し直す瞬間に TX が一瞬 low になり、DUT（X035 USART4）は framing error 付きの 1 byte を受けて
`testcmd.h` の行バッファに newline 無しで残していた。次の命令行がその byte の後ろに連結されて `unknown cmd=�PING 1` になる
（READY は出続けるので「DUT が命令を聞かなくなった」ように見える）。先に空行を送るか DUT を reset すると戻る。

- probe 側（oep-probe-arduino `FixtureUart`）: `begin()` の前に TX を INPUT_PULLUP → HIGH → OUTPUT の順で idle high に固定し、release では INPUT_PULLUP に戻す
  （`pinMode(OUTPUT)` 単体は一瞬 low を出す）。5 session 連続で PONG。
- core 側（`HardwareSerial::irq`）: FE / NE / PE の立った byte はリングに入れない（ORE は当該 byte が有効なので入れる）。fixture.gpio で
  DUT RX に 0.2 / 2 / 50 ms の low pulse を入れた直後でも PING が通る。
