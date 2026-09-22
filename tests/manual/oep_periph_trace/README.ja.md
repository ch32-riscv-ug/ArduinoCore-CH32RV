# oep_periph_trace — X035 の peripheral 出力を線上で実測する（worklist P3 行 4 / 5 / 7）

`periph_probe` sketch を OEP probe 経由で転送し、fixture.uart の命令で `analogWrite` / `tone` / `delayMicroseconds` /
`millis` / `SPI` を出させ、`fixture.capture`（PARLIO、1〜20 MHz sample）で線上を測って host 側で decode する。

```
uv run tests/manual/oep_periph_trace/oep_periph_trace.py                 # pwm / tone / timing / spi
uv run tests/manual/oep_periph_trace/oep_periph_trace.py --only spi
```

配線（E143 pin map）: PA1 → P4 GPIO47、SPI1 SCK PA5 → 4、MOSI PA7 → 5、MISO PA6 → 11、CS PA4（GPIO 駆動）→ 53。

## 2026-09-22 の実測（CH32X035F8U6、48 MHz、core `176f7c2` 以降）

| 項目 | 実測 | 備考 |
|---|---|---|
| `analogWrite(PA1, 64/128/192)` | 1003.5 Hz、duty 25.0 / 50.0 / 75.0 %（期待 25.1 / 50.2 / 75.3 %） | 周期 jitter 0.5 µs |
| `analogWrite(PA1, 255 / 0)` | 常時 high / 常時 low（edge 無し） | |
| `tone(PA1, 500 / 1000 / 4000)` | 500.3 / 1000.5 / 4000.0 Hz、duty 50 % | jitter ≤ 1 µs |
| `digitalWrite` 2 回（delay 無し） | 半周期 2.0 µs | **digitalWrite 1 回 ≈ 2 µs**（48 MHz で約 96 cycle、todo） |
| `delayMicroseconds(0/10/100/1000)` + digitalWrite | 半周期 5.0 / 13.75 / 104.25 / 1004.5 µs | **固定 +3〜4 µs**（micros() 2 回 + digitalWrite）。1 µs 単位の polling 実装 |
| `millis()` で 10 ms ごとにトグル ×20 | 周期 20.005 ms（期待 20.000）、jitter 2 µs、DUT 側 200 ms | SysTick 1 ms tick の精度 0.03 % |
| SPI1 mode 0/1/2/3 @1 MHz、4 byte | CPOL と MOSI 遷移エッジが 4 mode とも仕様どおり、data 一致、32 clock | 要求 1 MHz → 実 0.75 MHz（48 MHz / 64） |
| SPI1 @4 MHz / @250 kHz | 3.0 MHz（/16）/ 187.5 kHz（/256） | prescaler は 2 のべき、要求を超えない最大 |

SPI の decoder は最初「data が一致するエッジ」で CPHA を判定していて mode 1 を MISMATCH と誤判定した。MOSI の**遷移エッジ**
（CPHA=0 は trailing、CPHA=1 は leading）で判定するのが正しく、それで 4 mode とも一致した。

## 副産物

- `fixture.capture` は PARLIO の整数分周（PLL 160 MHz / ≤256）のため **650 kHz 未満では constant を返す**。200 kHz で `millis` 波形が
  取れなかったのはこれ。probe は下限を `min_clock_hz` で宣言し、未満の configure を reject するようにした。

## 2026-09-22（続き）: SPI の MISO 応答（worklist P3 行 7 の残り）

```
uv run tests/manual/oep_periph_trace/oep_periph_trace.py --only spi-peer
```

P4 に SPI slave の vendor tool `p4.spi-target`（SPI2_HOST、DMA 無し 64 byte、`arm(length, tx)` で 1 transaction を積み、CS が上がった後
`read_rx()` で MOSI 受信と bit 数を返す）を足し、同じ plan の `fixture.capture` で SCK/MOSI/MISO/CS を観測する。
X035 は 4 byte `a55a0f01` を送り、target は `3c96c30f` を返す。

| 条件 | DUT が受けた MISO | target が受けた MOSI | 線上 decode |
|---|---|---|---|
| 1 MHz（実 0.741 MHz）mode 0 / 1 / 2 / 3 | 一致 | 一致、32 bit | MISO / MOSI とも一致 |
| 250 kHz（実 0.185 MHz）mode 0 | 一致 | 一致 | 一致 |
| 4 MHz（実 2.857 MHz）mode 0 / 3 | 一致 | 一致 | 一致 |
| 12 MHz（実 6 MHz、/8）mode 0 / 3 | 一致 | 一致 | 20 MS/s では decode 不能、data のみ判定 |
| 24 MHz（実 24 MHz、/2）mode 0 | 一致 | 一致 | 同上。SCK 周波数は未計測 |

注意: DUT 初回の `SPI` command は `SPI.begin()` と CS の `pinMode` を含み、それまで CS が浮いているので slave が空 transaction を 1 つ消費した
（target rx 空・bits=0、DUT は FIFO に残っていた応答を受けて一見正常）。runner は arm の前に warm-up transaction を 1 回流す。

## 2026-09-22: CH32V003 の SPI peer（`--target v003 --only spi-peer`）

classic ESP32 probe には capture が無いので data の突き合わせのみ。SPI1 PC5/PC6/PC7、CS = PC3（→ GPIO17）。
mode 0〜3 @1 MHz 要求、mode 0 @250 kHz、mode 0/3 @4 MHz 要求: DUT の MISO 受信・target の MOSI 受信とも一致。
12 MHz 要求（実 6 MHz）以上は DUT 受信が 1 bit 遅れ（`1ecbe107` = 期待値 >> 1）: **probe 側 classic ESP32 SPI slave の限界**（MISO setup）。
PWM / tone / timing は capture が要るので V003 では未。

## 2026-09-22（続き）: CH32V003 の PWM / tone / timing（probe に GPIO sampler capture を追加、`--target v003`）

classic ESP32 probe の `fixture.capture` は core 0 の GPIO sampler（0.4〜2 MHz、1 byte/sample、window ≤ 164 ms）。

| 項目 | V003（48 MHz）実測 | 参考 X035 |
|---|---|---|
| `analogWrite` 64 / 128 / 192 | 1005.5 Hz、duty 25.0 / 50.0 / 75.0 %（期待 25.1 / 50.2 / 75.3）、jitter ≤ 3.5 µs | 1003.5 Hz、±0.3 % |
| `analogWrite` 255 / 0 | edge 無し、level 1.00 / 0.00 | 同 |
| `tone` 500 / 1000 / 4000 | 498.4 / 996.5 / 3984.1 Hz（−0.3〜−0.4 %）、duty 50 % | 誤差 < 0.1 % |
| `delayMicroseconds` 0 / 10 / 100 / 1000 | 半周期 17.75 / 26.0 / 113.3 / 1016.5 µs = **+16〜17 µs の一定 overhead、jitter 9〜15 µs** | +3〜4 µs |
| `digitalWrite` 2 連続 | 1.25 µs | ≈2 µs |
| `millis()` 10 ms toggle | 周期 20.090 ms（+0.45 %）、jitter 1 µs | 20.005 ms |

`delayMicroseconds` の +16 µs と jitter は V003 特有（X035 は +3〜4 µs）。core の実装（SysTick 比較の分解能、割込みの長さ）を見る価値がある（todo）。

### 同日追記: V003 の `delayMicroseconds` は core の `micros()` の除算だった

`micros()` の `ticks / CH32_TICKS_PER_US` が rv32ec（乗除算命令なし）では libgcc の除算になり、1 回数 µs。逆数の shift/add（各家系の
分周値で厳密、静的 assert 付き）に置き換え、さらに GCC が `__mulsi3` を呼ばないよう定数乗算を bit ごとに展開した。

| `delayMicroseconds` | 修正前（V003） | 修正後（V003） | X035 |
|---|---|---|---|
| 0 / 10 / 100 / 1000 の半周期 | 17.75 / 26.0 / 113.3 / 1016.5 µs、jitter 9〜15 µs | **5.5 / 14.5 / 104.3 / 1007.5 µs、jitter ≤ 2.5 µs** | 5.75 / 15.5 / 105.0 / 1005.8 µs |
| `millis()` 10 ms toggle | 20.090 ms | 20.059 ms | 20.008 ms |

### 同日追記: SPI の連続 transfer（X035、row 7 の残り）

64 byte の transaction × 3（DUT MISO 受信・target MOSI 受信とも一致、512 bit）と、4 byte × 5 連続（4 MHz mode 3、settle 無し）5/5。
sketch 側の引数バッファ（`%71s`）が 35 byte で切っていたので 135 に広げた。
