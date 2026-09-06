# harness の配線設計 — 各シリーズで何に繋げば何が試験できるか

文書状態: **提案**(`ch32-device-data` の生成物から機械的に導出。実配線・実測は未)
文書基準日: 2026-09-06
前提: [harness-probe](harness-probe.ja.md)(harness を採る/採らないの評価)。本文書はその**配線の中身**を詰める。
出典データ: `ch32-device-data` の `index/pinout.csv`(24983 行、part number 別・全 route)、
`index/parts.csv`、`evidence/debug_wiring.csv`。手書きのピン表は作らない([device-data](device-data.ja.md))。

想定 harness: **RP2040-Zero**。デジタルは `GP0`–`GP15` が連番でエッジに出ていて PIO の `in pins, 16` が
そのまま 16ch キャプチャになる。**アナログは窓の外**(`GP26`–`GP29`)。`GP16` は基板上 WS2812、
`GP17`–`GP25` は裏面パッド。

---

## 0. 結論(先に)

1. **24 series は既定 route で 6 群に分かれ、そのうち 10 series が完全に同一**。
   `USART1 = PA9/PA10`、`I2C1 = PB6/PB7`、`SPI1 = PA5/PA7/PA6/PA4`、`debug = PA13/PA14`。
   **1 本のケーブルで 10 series が挿し替えなしに載る**(§2)。
2. **16 本の窓に収まるケーブルを 5 本作れば全 24 series を覆える**(§4)。
   案A(**12 series**)、案B(7)、案C(2)、案E(2)、案M(1)。
   1 本あたり **16 本の窓 + 4 本のアナログ**で、すべて RP2040 の hardware ブロック割当まで検証済み。
3. **どの案も既定 route でバスの 3〜8 インスタンスに届く。**
   16 本で足りなくなるのは **2 本目以降の SPI と上位番号の USART** だけ。
   全インスタンスを同時に張ろうとすると **V303/V305/V307/V317 だけが 18 pad** 必要で窓に入らない(§3)。
4. **ADC・DAC・PWM は専用線が要らない。** バス線と同じ pad に相乗りしている。
   案A の 16 本のうち **ADC が 8 pad、PWM(TIM CHx)が 12〜15 pad、DAC が 2 pad**(V30x/V407 系)。
   窓の外に要るのは「DAC 出力の並列タップ」「VDD」「電流シャント」だけ(§6)。
5. **相手の数は UART×2 / I2C×2 アドレス / SPI×1 / GPIO×2〜4 で足りる**(§5)。
   RP2040 の hardware ブロック(UART0/UART1/I2C0/I2C1/SPI0)だけで賄え、
   **PIO は 16ch キャプチャと debug phy に丸ごと残せる**。
6. **pinmux の方式が 2 種類ある**(§2.1)。
   **AFIO remap 方式(20 series)は 1 route = 1 pad**。
   **AF 番号方式(V205 / X305 / X315)は 1 つの AF に複数 pad 候補があり、role ごとに独立に選べる**。
   後者のほうが逃げ道が多い。**コアの `setRoute()` がこの差をどう表現しているかは要確認**(§9-1)。
7. **配線の罠が 3 つ**(§4 の各案はこれを踏まないように組んである)。
   - **V003 系の `PC1`** が `I2C1.SDA` と `SPI1.NSS`(既定)と `USART1.RX`(remap-3)を兼ねる。
     → **NSS を `PC0`(remap-1)へ寄せれば衝突しない**(データ上 confirmed)。
   - **X033/X035 の `PC16`/`PC17` は USB D-/D+、`PC18`/`PC19` は SWD** で、
     どちらも `I2C1` の remap 先。**I2C を remap-2〜5 へ振ると USB か debug が死ぬ**。
   - **X305/X315 の `SPI1.SCK`/`MOSI` と `USART3` は `PA13`/`PA14`(SWD)にも置ける**。
     ただし `PA5`/`PA6`/`PA7` にも置けるので、**そちらを選べば避けられる**。

---

## 1. RP2040-Zero 側の制約

### 1.1 hardware ブロックが使えるピンは決まっている

GPIO と PIO はどのピンでもよいが、**hardware の UART / I2C / SPI は下表のピンでしか使えない**。

| ブロック | 使えるピン(`GP0`–`GP15` の範囲) |
|---|---|
| UART0 | TX `GP0` / `GP12`、RX `GP1` / `GP13` |
| UART1 | TX `GP4` / `GP8`、RX `GP5` / `GP9` |
| I2C0 | SDA `GP0` `GP4` `GP8` `GP12`(偶数)、SCL `GP1` `GP5` `GP9` `GP13`(奇数) |
| I2C1 | SDA `GP2` `GP6` `GP10` `GP14`、SCL `GP3` `GP7` `GP11` `GP15` |
| SPI0 | `GP0`–`GP3` または `GP4`–`GP7`(RX / CSn / SCK / TX の順) |
| SPI1 | `GP8`–`GP11` または `GP12`–`GP15`(同順) |

**SPI は 4 本連続、I2C は(偶数, 奇数)の隣接、UART は決まった 2 本**。
これが「プローブ側でも自由には振れない」の実体で、DUT 側の pad 割当より先に効く。

### 1.2 target 側として使うときの向き

harness は**バスの相手役**なので、SPI は slave、I2C は target になる。

| 役 | RP2040 側 | 向き |
|---|---|---|
| SPI slave(PL022 slave mode) | `RX` ← DUT MOSI / `TX` → DUT MISO / `SCK` ← DUT SCK / `CSn` ← DUT NSS | **SCK と CS は入力** |
| I2C target(DW_apb_i2c slave) | SDA 双方向 / SCL 入力(**clock stretch で引ける**) | **1 ブロック 1 アドレス** |
| UART peer | TX → DUT RX / RX ← DUT TX | — |

**I2C は 1 ブロック 1 アドレス**なので、2 アドレスを同時に演じるには
**I2C0 と I2C1 を別ピンに出す**か、PIO I2C target を書くかのどちらか。
本文書の案では前者を採る(**hardware だけで 2 アドレス**)。

### 1.3 アナログは窓の外

`GP26`–`GP29` の 4ch のみ、合計 500 kSa/s。**DAC は無い**ので、DUT の `analogRead` へ既知電圧を
与えるには PWM + RC か外付け DAC(`MCP4725` 等)が要る。

---

## 2. DUT 側 — 24 series は既定 route で 6 群に分かれる

各 series の**最大パッケージ品**について、`USART1` / `I2C1` / `SPI1` の既定 route pad を比較した。

| 群 | series 数 | USART1 TX/RX | I2C1 SCL/SDA | SPI1 SCK/MOSI/MISO/NSS | debug | series |
|---|---:|---|---|---|---|---|
| **A** | **10** | `PA9/PA10` | `PB6/PB7` | `PA5/PA7/PA6/PA4` | `PA13/PA14` | V103, V203, V208, V303, V305, V307, V317, V407, V467, L103 |
| **B** | **5** | `PD5/PD6` | `PC2/PC1` | `PC5/PC6/PC7/PC1` | `PD1`(+`PB3`) | V003, V002, V004, V006, V007 |
| B′ | 2 | `PD5/PD6` | `PC2/PC1` | 一部が別 pad | `PD1`/`PB3` | V005(MOSI `PA2` / MISO `PB3`)、M007(MOSI `PC0`) |
| **C** | **2** | `PB10/PB11`(X035)/ `PA10/PA11`(X033) | `PA10/PA11` | `PA5/PA7/PA6/PA4` | `PC18/PC19` | X033, X035 |
| **E** | **3** | AF 番号方式(§2.1) | 同左 | 同左 | `PA13/PA14` | V205, X305, X315 |
| D | 1 | `PC1/PC0` | `PB3/PB2` | `PA1/PC3/PC4/PA0` | `PA3/PA2` | M030 |
| F | 1 | remap のみ(既定 route 無し) | `PB6/PB7` | `PA5/PA7/PA6/PA4` | データ無し | M103 |

**読み方**:

- **群 A の 10 series は完全に同一**。ここが最大のまとまりで、コアの実機 bench(V103 / V203 / V307 / L103)も全部ここに入る。
- **M103 と V205 も案A のケーブルがそのまま通る**。M103 は pad が群 A と同一、V205 は AF 方式で pad 候補が広く群 A に重なる(§4.2)。
- **X033/X035 は `SPI1` が群 A と同じ pad**(`PA5/PA7/PA6/PA4`)。違うのは I2C と USART と debug だけ。

### 2.1 pinmux の方式が 2 種類ある(重要)

`pinout.csv` の `route` 列を series 別に見ると、**1 つの route に複数の pad 候補があるかどうか**で
はっきり 2 つに割れる。

| 方式 | series | 意味 |
|---|---|---|
| **AFIO remap 方式** | 20 series(群 A / B / B′ / C / D / F) | **1 route = 1 pad**。`PCFR1`/`PCFR2` の値を決めると pad が一意に決まる |
| **AF 番号方式** | **V205 / X305 / X315** | **1 つの AF 番号に複数 pad 候補**があり、**role ごとに独立に選べる**。例: X315 の `SPI1.SCK` は `af-4` のまま `PA5` / `PA13` / `PB1` のどれでもよい |

- V303 / V305 / V307 は AFIO 方式だが、**1 組だけ複数 pad の例がある**(混在)。
- **AF 方式のほうが配線の逃げ道が多い**。X305/X315 が 11 pad で全インスタンスに届くのはこれが理由。
- **コアの `setRoute()` / `setPins()` がこの差をどう扱っているかは未確認**(§9-1)。
  AFIO 方式なら「route 番号」で足りるが、AF 方式では **route 番号だけでは pad が決まらない**。

---

## 3. 全インスタンスを同時に張ると何本要るか

「その series の **USART / I2C / SPI の全インスタンス**を、配線を替えずに 1 つずつ試験できる」
ための最小 pad 数。全 route を対象に、§2.1 の 2 方式を区別したうえで beam search で求めた。
`ADC` / `PWM` / `DAC` 列は、**その最小 pad 集合の中にいくつ相乗りしているか**。

| series | U/I/S インスタンス | 最小 pad | ADC | PWM | DAC | 16ch 窓 |
|---|---|---:|---:|---:|---:|---|
| V003 / V002 / V004 | 1/1/1 | **3〜5** | 0 | 3 | – | 余裕 |
| V005 / V006 / V007 / M007 | 2/1/1 | **4** | 2 | 3–4 | – | 余裕 |
| M030 | 1/1/1 | **4** | 4 | 3 | – | 余裕 |
| X033 / X035 | 4/1/1 | **8** | 1–3 | 0–4 | – | 収まる |
| M103 | 4/2/1 | **9** | 2 | 8 | – | 収まる |
| **X305 / X315** | 4/2/3 | **11** | 5 | 6 | – | 収まる |
| V103 | 3/2/2 | **12** | 2 | 9 | – | 収まる |
| L103 | 4/2/2 | **12** | 1 | 8 | – | 収まる |
| V203 / V208 | 4/2/2 | **14** | 2 | 9 | – | 収まる |
| **V205** | 8/2/2 | **15** | 2 | 13 | – | 収まる(案A でも 8/12 に到達) |
| V407 / V467 | 10/1/3 | **15** | 4 | 4 | **2** | ぎりぎり |
| **V303 / V305 / V307 / V317** | 8/2/3 | **18** | 4 | 12 | **2** | **入らない** |

**要点**:

- **16 本の窓で「全インスタンス」に届かないのは V30x 系の 4 series だけ**。
  USART が 8 本ある割に AFIO 方式で逃げ道が少ないのが理由。
- この最小値は pad 数だけを最適化した結果で、**利用者が実際に使う既定 route から外れる**。
  実用案(§4)は既定 route を優先し、代わりに一部インスタンスを諦める。
- **DAC を持つのは V303/V305/V307/V317/V407/V467 の 6 series だけ**で、pad は `PA4`/`PA5` に固定。
  これは **`SPI1` の NSS/SCK と同じ pad**(§6)。

---

## 4. 実用案 — 16 本の割当(RP2040 の hardware ブロックまで検証済み)

### 4.1 harness 側の割当(全案で共通)

**この並びは全案で共通にする。**変えるのは DUT 側のケーブルだけ。

| GP | RP2040 の機能 | 役 |
|---|---|---|
| `GP0` / `GP1` | **UART0** TX / RX | UART peer #1 |
| `GP2` / `GP3` | **I2C1** SDA / SCL | I2C target #1 |
| `GP4` / `GP5` / `GP6` / `GP7` | **SPI0** RX / CSn / SCK / TX | SPI slave(MOSI / NSS / SCK / MISO) |
| `GP8` / `GP9` | **UART1** TX / RX | UART peer #2 |
| `GP10` / `GP11` | GPIO(SIO / PIO) | 刺激・観測・INT/DRDY |
| `GP12` / `GP13` | **I2C0** SDA / SCL | I2C target #2(2 つ目のアドレス) |
| `GP14` / `GP15` | PIO | **debug 線**(SWIO / RVSWD) |

**hardware ブロック 5 個(UART×2、I2C×2、SPI×1)がすべてピン制約を満たし、
PIO は 16ch キャプチャと debug phy に丸ごと残る。**
`in pins, 16`(base `GP0`)が 16 本全部を読むので、**自分が駆動した線も同じ時間軸で見える**。

`GP2`/`GP3` と `GP12`/`GP13` は I2C 以外にも振れる(`GP2`=SPI0 SCK、`GP3`=SPI0 TX、
`GP12`/`GP13`=UART0、`GP12`=SPI1 RX …)ので、**「I2C か GPIO か別のバスか」を実行時に切り替えられる**。
これが X033/X035 の `PA10`/`PA11` のように **1 組の線で 3 種のバスを回す**ときに効く。

### 4.2 案A — 12 series(V103 / V203 / **V205** / V208 / V303 / V305 / V307 / V317 / V407 / V467 / L103 / M103)

| GP | DUT pad | 主な役 | 相乗り |
|---|---|---|---|
| `GP1` / `GP0` | `PA9` / `PA10` | USART1 TX/RX(既定) | TIM1 CH2/CH3 |
| `GP2` / `GP3` | `PB7` / `PB6` | I2C1 SDA/SCL(既定) | USART1 remap-1、TIM4 CH1/CH2、TIM8 CH1/CH2 |
| `GP4` / `GP5` / `GP6` / `GP7` | `PA7` / `PA4` / `PA5` / `PA6` | SPI1 MOSI/NSS/SCK/MISO(既定) | **ADC IN4–IN7**、**DAC1/DAC2**(`PA4`/`PA5`)、TIM3 CH1/CH2、USART7 remap-1 |
| `GP9` / `GP8` | `PA2` / `PA3` | USART2 TX/RX(既定) | ADC IN2/IN3、TIM2 CH3/CH4 |
| `GP12` / `GP13` | `PB11` / `PB10` | I2C2 SDA/SCL(既定) | **USART3 RX/TX(既定)**、TIM2 CH3/CH4 |
| `GP10` / `GP11` | `PB0` / `PB1` | GPIO / EXTI(**別ポート・別 bit**) | ADC IN8/IN9、TIM3 CH3/CH4、USART4 remap-1 |
| `GP14` / `GP15` | `PA13` / `PA14` | SWDIO / SWCLK | — |

**被覆(データで検証)**

| series | 到達 | 届かないもの | ADC | PWM | DAC |
|---|---|---|---:|---:|---:|
| V103 | 6: I2C1, I2C2, SPI1, USART1–3 | SPI2 | 8 | 12 | – |
| V203 / V208 | 7: 上記 + USART4 | SPI2 | 8 | 12 | – |
| V303 / V305 / V307 / V317 | 8: 上記 + USART7 | SPI2, SPI3, USART5, USART6, USART8 | 8 | 13 | **2** |
| V407 / V467 | 8: I2C1, SPI1, USART1–4, USART7, USART9 | SPI2, SPI3, USART5, USART6, USART8, USART10 | 8 | 12 | **2** |
| L103 | 7: I2C1, I2C2, SPI1, USART1–4 | SPI2 | 8 | 15 | – |
| M103 | 7: I2C1, I2C2, SPI1, USART1–4 | **なし** | 8 | 13 | – |
| **V205** | 8: I2C1, I2C2, SPI1, USART1–3, USART4, USART7 | SPI2, USART5, USART6, USART8 | 8 | 14 | – |

**16 本で 6〜8 インスタンス、ADC 8 pad、PWM 12〜15 pad、DAC 2 pad に届く。**
届かないのは 2 本目以降の SPI と、上位番号の USART。

### 4.3 案B — V003 系 7 series(V003 / V002 / V004 / V005 / V006 / V007 / M007)

| GP | DUT pad | 役 |
|---|---|---|
| `GP1` / `GP0` | `PD5` / `PD6` | USART1 TX/RX(既定)。**ADC IN5/IN6 も兼ねる** |
| `GP2` / `GP3` | `PC1` / `PC2` | I2C1 SDA/SCL(既定) |
| `GP4` / `GP5` / `GP6` / `GP7` | `PC6` / **`PC0`** / `PC5` / `PC7` | SPI1 MOSI/**NSS**/SCK/MISO |
| `GP8`–`GP13` | `PA1` `PA2` `PC3` `PC4` `PD2` `PD3` | GPIO / ADC / PWM |
| `GP14` / `GP15` | `PD1` / `PB3` | **SWIO**(1 線)/ SWCLK(V003 以外) |

**被覆**: 7 series すべてで `USART1` / `I2C1` / `SPI1` が既定 route で通り、**届かないものはゼロ**。
V005/V006/V007/M007 では `USART2` も `PD2`/`PD3`(remap-3)で通る。ADC 6〜7 pad、PWM 12〜15 pad。

> **`PC1` 問題は `PC0` で回避できる。** `PC1` は `I2C1.SDA`(既定)と `SPI1.NSS`(既定)と
> `USART1.RX`(remap-3)を兼ねるが、**`SPI1.NSS` は remap-1 で `PC0` に出せる**
> (`pinout.csv` 上 confidence=confirmed)。上表はそれを使っている。
> コアの `SPI.setRoute()` がこの route を出しているかは**要確認**(§9-3)。
> 出していなければ、CS はスケッチが GPIO で駆動する(Arduino の慣習どおり)ことで回避できる。
>
> **未配線で残るのは `PD0` / `PD4` / `PD7` の 3 pad だけ**。20 pin の V003 は GPIO が 18 本しかないので、
> **チップのほぼ全部が窓に入る**。これが「1 device 専有」がいちばん効く例。

### 4.4 案C — X033 / X035

| GP | DUT pad | 役 |
|---|---|---|
| `GP1` / `GP0` | `PB10` / `PB11` | USART1 TX/RX(X035 既定) |
| `GP2` / `GP3` | `PA11` / `PA10` | **I2C1 SDA/SCL(既定)**。X033 の USART1 remap-1、SPI1 remap-2 でもある |
| `GP4` / `GP5` / `GP6` / `GP7` | `PA7` / `PA4` / `PA5` / `PA6` | SPI1 MOSI/NSS/SCK/MISO(既定、**群 A と同じ**) |
| `GP9` / `GP8` | `PA2` / `PA3` | USART2 TX/RX(既定) |
| `GP10` / `GP11` | `PB0` / `PB1` | USART4(既定)/ GPIO |
| `GP12` / `GP13` | `PA8` / `PA9` | GPIO / SPI1 remap |
| `GP14` / `GP15` | `PC18` / `PC19` | **SWD**。USART3 remap-1 でもある |

**被覆**: X033 / X035 とも **6/6 インスタンス全部に到達**(I2C1, SPI1, USART1–4)。ADC 8 pad。

> `PC16`/`PC17` は **USB D-/D+**、`PC18`/`PC19` は **SWD**。どちらも `I2C1` の remap 先
> ([upload-and-fixture](upload-and-fixture.ja.md) の X035 制約)なので、
> **I2C を remap-2〜5 へ振ると USB か debug のどちらかが死ぬ**。案C は既定 route を使うのでこれを踏まない。

### 4.5 案E — X305 / X315

`SPI1` は `PA13`/`PA14`(SWD)にも `PA5`/`PA6`/`PA7` にも置ける(§2.1 の AF 方式)。**後者を選ぶ。**

| GP | DUT pad | 役 |
|---|---|---|
| `GP1` / `GP0` | `PD4` / `PD5` | USART1 TX/RX(`af-1`)。**I2C1 SCL/SDA(`af-3`)と同じ pad** |
| `GP2` / `GP3` | `PD7` / `PD6` | I2C2 SDA/SCL(`af-3`)。USART2 でもある |
| `GP4` / `GP5` / `GP6` / `GP7` | `PA6` / `PA4` / `PA5` / `PA7` | SPI1 MOSI/SCS/SCK/MISO(`af-4`、**SWD を避けた選択**) |
| `GP9` / `GP8` | `PC8` / `PC9` | USART3 TX/RX(`af-1`、`PA13`/`PA14` を避けた選択) |
| `GP10` / `GP11` | `PB12` / `PB13` | USART4 / SPI2 |
| `GP12` / `GP13` | `PB14` / `PB15` | SPI2 MOSI/MISO |
| `GP14` / `GP15` | `PA13` / `PA14` | SWD |

**被覆**: 両 series とも **8/9**(I2C1, I2C2, SPI1, SPI2, USART1–4)。届かないのは `SPI3`(`PD11`–`PD13`)。ADC 8 pad。

### 4.6 案M — M030

既定 route で `USART1` `I2C1` `SPI1` の全部(1 インスタンスずつ)に到達。**ADC 11 pad、PWM 13 pad**と相乗りが多い。

| GP | DUT pad | 役 |
|---|---|---|
| `GP1` / `GP0` | `PC1` / `PC0` | USART1 TX/RX(既定) |
| `GP2` / `GP3` | `PB2` / `PB3` | I2C1 SDA/SCL(既定) |
| `GP4` / `GP5` / `GP6` / `GP7` | `PC3` / `PA0` / `PA1` / `PC4` | SPI1 MOSI/NSS/SCK/MISO(既定) |
| `GP8`–`GP13` | `PA4` `PA5` `PA6` `PA7` `PB5` `PB6` | GPIO / ADC / PWM |
| `GP14` / `GP15` | `PA3` / `PA2` | SWDIO / SWCLK(**1 線 / 2 線の両対応**) |

### 4.7 まとめ

| 案 | series 数 | 被覆インスタンス | 対象 |
|---|---:|---|---|
| A | **12** | 6〜8 | V103, V203, V205, V208, V303, V305, V307, V317, V407, V467, L103, M103 |
| B | **7** | 3〜4(全部) | V003, V002, V004, V005, V006, V007, M007 |
| C | **2** | 6(全部) | X033, X035 |
| E | **2** | 8 / 9 | X305, X315 |
| M | **1** | 3(全部) | M030 |

**5 本のケーブルで 24 series 全部**。うち **案A の 1 本で 12 series** が挿し替えなしに載る。
案A を X305/X315 に当てると 4/9 にしか届かないので、そこだけは別ケーブルが要る。

---

## 5. 相手は何台要るか

### 5.1 同時に必要な peer の数

| 相手 | 必要数 | 実現 | 理由 |
|---|---:|---|---|
| **UART peer** | **2** | hw UART0 + UART1 | 1 本を試験、もう 1 本を別インスタンスへ。3 本目以降は PIO UART で足せる |
| **I2C target** | **2 アドレス** | hw I2C0 + I2C1 | `I2C1` と `I2C2` を同時に、または 1 バス上に 2 デバイスを置く走査試験 |
| **SPI slave** | **1** | hw SPI0(PL022 slave) | `SPI2`/`SPI3` は配線替え。2 本目が要るなら hw SPI1 をピンが許す範囲で |
| **GPIO** | **2〜4** | SIO / PIO | EXTI を**別ポート・別 bit 番号**で 2 本(EXTI 線は bit 番号で決まる)、marker、INT/DRDY |
| **アナログ入力** | **2〜4** | `GP26`–`GP29` | DAC 出力、VDD、電流シャント |
| **アナログ出力** | **1〜2** | PWM+RC(裏面パッド)or 外付け DAC | `analogRead` に既知電圧を与える |

**hardware ブロックだけで足りるので、PIO は全部キャプチャと debug phy に残る。**
PIO I2C target(多アドレス)は「あると嬉しい」であって、**必須ではない**。

### 5.2 制御チャネルを UART から追い出せる

コアの HIL protocol(`READY?` → arm → `RUN` → `EVENT` → `DONE`、[test-strategy](test-strategy.ja.md))は
いま **DUT の UART を 1 本占有する**。V003 のように USART が 1 本しかない部品では、
**制御に使った瞬間に UART の試験ができなくなる**。

harness は DMI を持つので、**制御チャネルを `SerialRTT` / `SerialDMDATA`(debug 線経由)へ逃がせる**。
そうすると **USART は全部試験対象に回せる**。これは LinkE + 外部 LA では作れない構成で、
[harness-probe](harness-probe.ja.md) §5-9 の依頼(3 経路を native に扱う)がここで効く。

---

## 6. ADC / DAC / PWM は専用線が要らない

**バス線と同じ pad に相乗りしている。**案A の 16 本での実測値:

| 機能 | 群 A で相乗りする pad | 本数 |
|---|---|---:|
| **ADC** | `PA2` `PA3` `PA4` `PA5` `PA6` `PA7`(IN2–IN7)、`PB0` `PB1`(IN8/IN9) | **8** |
| **PWM(TIM CHx)** | `PA9`/`PA10`(TIM1)、`PA2`/`PA3`/`PB10`/`PB11`(TIM2)、`PA6`/`PA7`/`PB0`/`PB1`(TIM3)、`PB6`/`PB7`(TIM4) | **12〜15** |
| **DAC**(V303/305/307/317/407/467 のみ) | `PA4` = DAC1、`PA5` = DAC2 | **2** |

→ **窓の 16 本はデジタルのまま**でよい。窓の外(`GP26`–`GP29`)に要るのは次だけ。

| GP | 用途 |
|---|---|
| `GP26` / `GP27` | **`PA4` / `PA5` の並列タップ**(DAC 出力測定)。SPI slave では CSn と SCK は入力なので、同じ pad に harness 側 2 ピンを挿しても競合しない(**要実測**、§9-5) |
| `GP28` | DUT の VDD 分圧(立ち上がり・brown-out) |
| `GP29` | 電流シャント(低消費電力モード、[harness-probe](harness-probe.ja.md) §2.2) |

`analogRead` へ既知電圧を与える線は窓にも `GP26`–`GP29` にも空きが無いので、
**裏面パッド(`GP17`–`GP25`)から 1〜2 本出す**。
→ コネクタは **16(窓) + 4(アナログ) + 1〜2(刺激) + VDD / GND / VREF** が目安。

---

## 7. 試験できないもの / 相手が harness ではないもの

コアの[ペリフェラル対応表](peripheral-support.ja.md)の全項目を 4 段階に分けた。

### 7.1 ◎ harness が相手役になれる(追加部品なし)

GPIO、EXTI、USART(`Serial`)、I2C(`Wire`、master/slave 両方)、SPI(`SPI`)、TIM(PWM / tone / 入力捕捉)、
ADC(既知電圧を与える)、DAC(測る)、SysTick / `millis` / `micros`、`shiftOut` / `shiftIn` / `pulseIn`、
SoftSPI / SoftWire / SoftSerial、**ARGB**(1 線。PIO でデコードできる)、
**PIOC**(X033/X035/V205 の 2 ピンプロトコルエンジン。相手役になれる)。

### 7.2 ○ 相手が要らない(観測・自己完結)

| ペリフェラル | 見方 |
|---|---|
| RCC / クロック | `MCO` を窓の 1 本へ出して周波数を測る。**probe が RCC を書き換えないことが前提**([harness-probe](harness-probe.ja.md) §5-1) |
| PFIC / 割込優先度 | DMI でレジスタを読む(方法3)+ 刺激 GPIO で順序を見る |
| IWDG / WWDG | reset の発生をキャプチャで見る |
| PWR / 低消費電力 | **電流シャント**(`GP29`)。いまコアで完全に未実装の項目 |
| FLASH 自己書換(EEPROM 相当) | DMI で読み返す |
| RTC | 長時間。`RTC_OUT` を pad へ出せれば 1 Hz を測れる |
| BKP / CRC / RNG | 相手不要 |
| OPA / コンパレータ | アナログ入力を PWM+RC で与え、出力をデジタルで見る |

### 7.3 △ 追加部品が要る(harness 単体では不可)

| ペリフェラル | 対象 series | 何が要るか |
|---|---|---|
| **CAN** | V203/V205/V208/V303/V305(2ch)/V307(2ch)/V317(2ch)/V407/V467/L103/M103 | **CAN トランシーバ**(`SN65HVD230` 等)+ RP2040 側は PIO CAN(`can2040`)。線は TX/RX の 2 本。**成立見込みはあるが要検証** |
| **I2S** | V303/V305/V307/V317/V407/V467 | PIO で slave/master を書く。3〜4 本。追加部品は不要だが**PIO の命令メモリを食う** |
| **TouchKey** | V00x/M007/V103/V20x/V30x/L103/X03x | 既知容量をアナログスイッチで切り替える治具 |
| **I3C** | V407/V467 | legacy I2C mode なら ◎。SDR 12.5 MHz の本来の I3C は要検証 |
| **USB PD** | X033/X035/L103/M030/V205/V407 | **CC 線の PD PHY が要る**。RP2040 単体では不可。`FUSB302` を足すか、**もう 1 枚 CH32X035 を PD の相手にする**(コアの `USBPD` ライブラリがそのまま使える)のが現実的 |
| **SDIO** | V303/V305/V307/V317/V407/V467 | 4 bit SDIO を演じるのは難度が高い。**SPI モードの SD カードなら容易**(EmbedBench に `unit_sdcard_model` が既にある) |

### 7.4 ✕ harness の役ではない / ピン数が足りない

| ペリフェラル | 理由 | 誰が相手か |
|---|---|---|
| **USB FS / HS / SS(device)** | RP2040 の USB は **device か host のどちらか一方**。PC 接続に使っている以上 host になれない。`Pico-PIO-USB` で 2 つ目の FS host を PIO に立てる手はあるが、**PIO をキャプチャと取り合う**(要検証) | **PC が相手**。DUT を直接 PC に挿すのが正しい。harness は同期と観測に回る |
| **Ethernet**(V203/V208 10M、V307 1G MAC+10M PHY、V317/V407/V467 MAC+10M/100M) | RMII/MII は 50 MHz・多ピン。実 PHY が要る | **PHY モジュールとスイッチ / PC** |
| **FSMC / PSRAM / LTDC / DVP** | 並列バスで 16〜30 本。**16ch の窓に入らない** | 専用治具。コアでも `対象外` |
| **QSPI** | 6 本 + 高速 | 専用治具 |
| **BLE**(V208) | 専用スタックと無線 | 対象外(コアも `対象外`) |
| **USB SS**(X315) | 5 Gbps | 対象外 |

### 7.5 harness 側の電気的な限界

- **RP2040 は 5V トレラントではない。** CH32V003 は 3.3 / 5.0 V 動作なので、
  **5 V で動かしている board があるなら分圧かレベル変換が要る**(ベンチの実態は未確認、§9-6)。
- target 向きの全線に直列抵抗(数百 Ω〜1 kΩ)を入れるのを既定にする(両側が同時に駆動したときの保護)。
- I2C の pull-up の所在(DUT 側 / harness 側 / 両方)を申告できるようにする。

---

## 8. リマップの扱い — 配線を増やすのではなく DUT を寄せる

**配線は高コストで差し替えられない**、という前提が効くのはここ。

`pinout.csv` を見ると、**同じ pad が複数のバス信号を受けられる**。群 A の例:

| pad | 受けられるバス信号(全 route) | 相乗り |
|---|---|---|
| `PA6` / `PA7` | `SPI1.MISO`/`MOSI`、`USART1.TX`/`RX`(remap-3)、`USART7.TX`/`RX`(remap-1) | ADC IN6/IN7、TIM3 CH1/CH2 |
| `PB6` / `PB7` | `I2C1.SCL`/`SDA`、`USART1.TX`/`RX`(remap-1) | TIM4 CH1/CH2、TIM8 CH1/CH2 |
| `PB10` / `PB11` | `I2C2.SCL`/`SDA`、`USART3.TX`/`RX` | TIM2 CH3/CH4 |

したがって方針は次になる。

> **配線済みの pad へ、DUT 側の `setRoute()` / `setPins()` でペリフェラルを寄せる。**
> harness の線を増やすのではなく、**DUT 側を動かす**。

これは副作用として **remap 機能そのものの機能試験になる**。
いまコアは `reg_probe` で **レジスタの値**(`PCFR1`/`PCFR2` のフィールドと pad の設定・解放)まで
確認しているが、**「remap 後に実際に信号がその pad から出るか」は未確認**
([TEST_PLAN](../tests/TEST_PLAN.ja.md) の AFIO remap 行)。案A〜M はそれを塞ぐ。

ただし逃げ道の量は series で大きく違う。

- **USART は route が多い**(V307 の USART1 は 4 route、X035 の USART4 は 6 route)ので寄せやすい。
- **I2C と SPI は route が 1〜2 本しか無い** series が多く、**寄せられない**。
  → `I2C2` / `SPI2` / `SPI3` は配線替えが要る、という §4 の結論はここから来ている。
- **AF 方式(V205 / X305 / X315)は role ごとに pad を選べる**ので、いちばん寄せやすい(§2.1)。

---

## 9. 未決・要確認

1. **AF 方式(V205 / X305 / X315)をコアがどう表現しているか**(§2.1)。
   `setRoute(n)` の `n` は AFIO 方式なら `PCFR` の値だが、AF 方式では **role ごとに pad が独立**なので
   route 番号だけでは決まらない。`reg_probe` の期待値生成にも同じ問題があるはず。
2. **M103 の debug pad** が `debug_wiring.csv` に無い。`USART1` の既定 route も無く remap のみ。
3. **V003 系の `SPI1.NSS = PC0`(remap-1)をコアが出しているか**(§4.3)。
   出していれば `PC1` の衝突が消える。
4. **X033 の `USART1` 既定 route が `PA10`/`PA11`** で **`I2C1` 既定と同じ pad**。
   案C はこれを 1 組の線で兼ねているが、**同時使用はできない**。試験順序で解く。
5. **`GP26`/`GP27` の並列タップ**(1 つの DUT pad に harness 2 ピン)が電気的に問題ないか。
   DAC がアナログ中間電位を出しているとき、デジタル入力バッファの貫通電流が出ないか。**要実測**。
6. **5 V 動作の board がベンチにあるか**(§7.5)。
7. **コネクタの物理形**。16(窓)+ 4(アナログ)+ 1〜2(刺激)+ 電源 で 24 ピン前後。
   [upload-and-fixture](upload-and-fixture.ja.md) が「後から変更するコストが最も高い項目」と
   書いている部分なので、実配線の前に決める。
8. **小さいパッケージでの再計算**。本文書は series ごとに **pin 数が最大の part number** を使った。
   小パッケージでは pad が bond されず route が減るので、**実際に使う board の part number で引き直す**。

---

## 10. 再現方法

本文書の表は次のデータから機械的に導出した。手で編集しないこと。

| 表 | 出典と方法 |
|---|---|
| §2 の群分け | `index/pinout.csv` の `route=default` 行を series 別に比較 |
| §2.1 の方式判定 | 同 `(peripheral, route, role)` が 2 つ以上の pad を持つ組の数 |
| §3 の最小 pad 数 | 全 route を対象に、role ごとの pad 候補から 1 つずつ選んで union を最小化(beam search) |
| §4 の被覆 | 提案 pad 集合に対し、各インスタンスが全 role を満たす組合せを持つかを判定 |
| §6 の相乗り | `peripheral` が `ADC*`(role `IN*`)/ `TIM*`(role `CH1`–`CH4`)/ `DAC*`(role `OUT`)の行を pad で突き合わせ |
| §7 の series 別有無 | `index/parts.csv` と[ペリフェラル対応表](peripheral-support.ja.md) |
| debug pad | `evidence/debug_wiring.csv`(出典 WCH-Link User Manual、confidence=confirmed) |

> **注意**: `(peripheral, route, role)` が複数 pad を持つ series(§2.1 の AF 方式)を
> 「1 route = 1 pad」として集計すると、**候補を取りこぼして必要 pad 数を過大に見積もる**。
> 最初の集計はこれを踏み、V205 を 20 pad(実際は 15)、X305/X315 を 14 pad(実際は 11)と誤った。
