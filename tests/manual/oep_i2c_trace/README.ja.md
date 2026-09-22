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

## 2026-09-22 の結果（worklist B「X035 から P4 0x42 へ write すると NACK」）

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
**残る容疑**: X035 の短い SDA hold（0.2〜0.4 µs）か遅い立上り（4 µs）に対する ESP32-P4 slave の感度。確認には hold と立上りを
制御できる master が要る（peer の IDF master は timing register を transaction ごとに書き戻すので変えられない）。台帳候補 `x035-p4-slave-no-ack`。

**注意**: `p4.i2c-target` の fixed-rx は v1 driver の制約で「address 後に終わった transaction」でも rx_done が発火し、長さ情報が無いので
古い buffer 内容を frame として返す。frame の存在を ACK の証拠にしないこと（このため 4 case とも `target rx=10111213` と出る）。
