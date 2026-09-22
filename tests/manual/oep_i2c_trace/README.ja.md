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

## 2026-09-22（続き）: read / repeated START / 400 kHz と、`requestFrom` が止まる core バグ

```
uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --rw
```

P4 target の preloaded-tx slot から X035 が読む。最初の run は **N byte 読むと N−1 byte で止まり永久に戻らなかった**（4 → 3、3 → 2、5 preload でも 3）。
halt して読むと I2C1 は BTF=1 / RXNE=1 / ACK=0 で master 自身が SCL を伸ばし、PC は `crt0_ch32.S` の default trap handler、
`mcause=2`（illegal instruction）、`mtval=0x300474f3` = `csrrci a5, mstatus, 8`、`mepc` は Wire.cpp の `NoIrq`。
**X035/X033 の sketch は User mode で走る（startup の mstatus=0x88、WCH の EVT も同じ）ので `mstatus` の CSR 操作は illegal**。
`Arduino.h` の `interrupts()` と `ch32_timer.c` は CSR 0x800 で回避済みだったが、Wire の `NoIrq` だけが直接 `mstatus` を触っていた。
`ch32_irq_save()/ch32_irq_restore()` を Arduino.h に置いて 3 箇所を統一。wire_selftest は NACK 経路しか通らないので気付けなかった。

修正後:

| 条件 | Wire | 線上 |
|---|---|---|
| `requestFrom(0x42, 4)` × 2 slot、100 kHz | got=4、data 一致 | `S 85A a1A b2A c3A d4N P` / `S 85A 11A 22A 33A 44N P` |
| `endTransmission(false)` → `requestFrom(4)` | rc=0、got=4 | `S 84A 01A 02A S 85A 55A 66A 77A 88N P`（repeated START） |
| write 4 byte @400 kHz | rc=0、target 受信一致 | `S 84A 0aA 0bA 0cA 0dA P`、SCL 周期 2.6 µs（≈385 kHz） |

`p4.i2c-target` の preloaded-tx は「N byte 読ませるには N+1 byte 積む」（E150）を firmware が filler で吸収している。

## 2026-09-22（続き）: clock stretch と stuck bus（worklist P3 行 6 の残り）

```
uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --stretch
uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --stuck
```

### clock stretch — target が SCL を伸ばす

`p4.i2c-target` に `set_stretch <us>`（op 0x11）を足した。ESP-IDF v1 slave driver は `stretch_en` で hardware stretch
（master read の address 一致時など）を有効にするが**解放を誰もしない**（raw flag が立ったまま SCL low が続き、最初の run は Wire が
25 ms で timeout、capture 全域 SCL low）。probe は `service()` で stretch を見つけてから `stretch_us` 待って `slave_scl_stretch_clr`
する。Wire の既定 timeout は `CH32_WIRE_TIMEOUT_US` = 25 ms。

| stretch | Wire `requestFrom(0x42, 4)` @100 kHz | 線上 |
|---:|---|---|
| 0 | got=4、一致、0.48 ms | `S 85A a1A b2A c3A d4N P` |
| 0.1 / 1 / 5 / 20 ms | got=4、一致、t = 0.57 / 1.47 / 5.53 / 20.47 ms | SCL low 最大 0.10 / 1.00 / 5.06 / 20.00 ms、それ以外は同じ |
| 30 ms（timeout 超） | got=0、**25.12 ms で諦める**、`getWireTimeoutFlag()`=1 | `S 85A a1N P`（解放後に peripheral が 1 byte 分だけ動いて STOP） |
| その直後、stretch off | got=4、一致 | `_needs_recovery` → SWRST 経路で回復 |

### stuck bus — 線が握られた時の Wire

| 状況 | `endTransmission` | 備考 |
|---|---|---|
| P4 GPIO が SDA を low に固定 | rc=5、**36 µs**（timeout flag 無し = error flag 経路、ARLO/BERR） | 2 回目は SWRST 後 START 待ちで 25 ms timeout |
| P4 GPIO が SCL を low に固定 | rc=5、25.0 ms、timeout flag=1 | 2 回目も同じ |
| 線を解放（誰もいない） | rc=2（address NACK）、0.12 ms | 回復 |
| target が byte 途中で SDA low を保持（`STUCK`: START, addr\|R, byte1=0x00 を ACK、次 byte の MSB で SCL high のまま放置） | `Wire.begin()` → rc=5、25 ms、2 回とも | **Wire の recover()（SWRST）では解放できない**。core に bus clear は無い |
| sketch から 9 pulse bus clear（`BUSCLR`） | 8 pulse で SDA=1、STOP | その後 write rc=0、target 受信一致 |

### fixture の事実: route 2 には bus pull-up が無い

capture で観測すると、X035 が PC16/PC17 を INPUT / open-drain release にした時の線は **0.00**（P4 I2C target が pin を持っていても同じ。
IDF slave driver は内部 pull-up を有効にせず、外付けも無い）。INPUT_PULLUP にすると 1.00。Wire が動くのは X035 の AF 出力が high を
能動的に駆動するから（X0 GPIO block、errata `x035-no-gpio-open-drain` の AF 側の顔）。P4 slave はそれに対して low を通せる（ACK、20 ms stretch）。
このため emulated open-drain の bit-bang（`BB drive=od`）はこの fixture では bus が動かず、`STUCK` / `BUSCLR` は SCL を push-pull、
SDA 解放を INPUT_PULLUP で行う。RM p.222 の `R8_UDEV_CTRL.RB_UD_PD_DIS`（既定 0 = UDP/UDM 内部 pull-down 有効、GPIO mode でも効く）
が「INPUT で 0」に寄与しているかは未分離。

**同日追記**: probe の `p4.i2c-target` が slave 生成後に SDA/SCL へ P4 内部 pull-up（GPIO matrix 経由、約 45 kΩ）を掛けるようにした。
target が pin を持つ plan では X035 の INPUT / OD release が線上 1.00 になり、open-drain の master でも bus が成立する。P4 GPIO を
直接使う plan（gpio / capture のみ）では従来どおり pull-up は無い。

## 2026-09-22: CH32V003 の Wire（`--target v003 --rw`）

route 0（PC2 SCL / PC1 SDA → ESP32 GPIO18 / 19、board pull-up 2.2 kΩ）。read 4 byte @100 kHz 一致、repeated START（write 2 → read 4）一致、
write 4 byte @400 kHz target 受信一致。2 つ目の preloaded slot だけ `00112233`（先頭に filler 0x00、末尾欠け）: P4 では E150 の
「slot + filler 1 byte」契約で揃うが、classic ESP32 の I2C slave（v1 driver、別 hardware）では前 slot の filler が次 read の先頭に出る。
**probe 側の実装差**で、V003 の Wire は正しい（記録のみ、修正は後）。

同日追記: probe に GPIO sampler capture が入り、classic ESP32 の I2C slave の filler 差も直したので、V003 でも線上 decode 付きで全一致:
`S 85A a1A b2A c3A d4N P` / `S 85A 11A 22A 33A 44N P` / repeated START `S 84A 01A 02A S 85A 55A 66A 77A 88N P` / 400 kHz write `S 84A 0aA 0bA 0cA 0dA P`
（2 MHz sampling なので 400 kHz の SCL 周期の値は当てにならない、byte と ACK は正しく読める）。
