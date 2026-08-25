# 実験0015: ch32-device-data `155c398` の取り込み検証

日付: 2026-08-25
比較: `b1285de`(現pin) → `155c398`(上流HEAD)
関連: [device-data](../device-data.ja.md)、[todo](../todo.ja.md)

上流のworking treeは**読むだけ**(dirtyのまま触っていない)。
`git clone --no-hardlinks`でscratchpadへ複製し、そのcommitを`--tables`に渡している。

基準線の確認: `generate.py --tables <b1285de> --check` は **exit 0 / DRIFT無し**。
つまり今のrepositoryは`b1285de`と完全に一致していて、以下の差分は純粋に上流の差。

---

## 1. run-onは直っている

同じ機械的検出(新表にしか無いsignal名のうち、旧表が知る2つへ綺麗に割れるもの)。

| | c2c457dのとき | 155c398 |
|---|---:|---:|
| run-on形の新signal名 | **28** | **4** |

残る4件はすべて**検出器の誤検出**で、run-onではない。

| 名前 | 割れ方 | 実際 |
|---|---|---|
| `ISOURCE1` | `ISOUR` + `CE1` | 正しい名前。旧表の`ISOUR`/`CE1`のほうが壊れていた |
| `VDDIO` | `VDD` + `IO` | 正しい名前 |
| `ETH_RMII_PPS_OUT` | `ETH_RMII_PPS_O` + `UT` | 正しい名前。旧表の`ETH_RMII_PPS_O`が切れていた |

## 2. 切れ(truncation)も直っている

旧表にあって新表から消えたsignal名 **211件**(pin_functions)。
うち61件は「新表により長い名前がある」形で、残りも大半が同種のゴミ。

```
MC → MCO      LTDC_V → LTDC_VSYNC     N → NC        OSC_OU → OSC_OUT
DD → VDD      DDA → VDDA              BAT → VBAT    C_G0 → LTDC_G0
I2S3_CK（12） → I2S3_CK  (脚注記号が剥がれた)
```

`remap_routes.csv`も同様(59件中32件が切れの解消)。

## 3. 「原因不明」だった2件は**解決していて、しかも正しい**

どちらも**英語表と中国語表の食い違い**で、上流が中国語表を採った。

### CH32M030 PB2/PB3

```
b1285de:  TIM3_CH1 ,default, reference, pin-table:en     ← 英語表
          TIM3_CH1N,default, reference, pin-table:zh     ← 中国語表
155c398:  TIM3_CH1N,default, confirmed, pin-table:zh+en  ← en行が消え、確定
```

### CH32V205ボード(実体は`CH32V203CCT6`) PB1

```
b1285de:  TIM1_CH3 ,default, reference, pin-table:en
          TIM1_CH3N,af-0   , reference, pin-table:zh
155c398:  TIM1_CH3N,af-0   , confirmed, pin-table:zh+en
```

**つまり以前のPWM pad(M030 PB2/PB3、V205 PB1)は誤読由来だった。**
いずれも相補出力(`…N`)で、`analogWrite()`が扱える対象ではない
(生成器は`…N`を意図的に除外している)。**消えるのが正しい。**

## 4. 我々の生成物への影響

`generate.py --tables <155c398> --check` → **8 additive / 15 rewriting**。

### 増える(良い変化)

- **PC13/PC14/PC15が7 board(V103 / V203 / V205 / V208 / L103 ほか)で追加**。
  上流が`PC13-TAMPER-RTC`のような装飾付きpad名の行を拾えるようになったため。
  この記法自体は新しくない(`PA0-WKUP`は`b1285de`に553行ある)
- **linker script 8本追加**: V303 128K/32K・256K/64K、V305 ×2、V307、V317、X305、X315
- V208 `CH32_SERIAL4_ROUTES`(2 route)追加、M103 SPI1 route 2→3

### 直っている(c2c457dで壊れていたもの)

| | c2c457d | 155c398 |
|---|---|---|
| V407/V467 SPI3既定route | PB3/4/5 → PC10/11/12へ変化 | **変化なし** |
| V208 ADC 16→9 / USART3 remap-1化 | 発生 | **発生しない** |
| V007 I2C1 route 2消失 | 発生 | **DRIFT無し** |
| X033 I2C1 route 5消失 | 発生 | **DRIFT無し** |

### 判断が要るもの

**(a) flash容量が6 boardで縮む。** `upload.maximum_size`が変わる。

| board | 旧 | 新 |
|---|---:|---:|
| CH32V303 | 480K | **128K** (RCT6等は256K) |
| CH32V305 | 224K | **128K** |
| CH32V307 | 480K | **256K** |
| CH32V317 | 480K | **256K** |
| CH32X305 | 480K | **192K** |
| CH32X315 | 480K | **192K** |

`products.csv`の`flash_bytes`が`491520`→`131072`等へ。confidenceもbasisも
`confirmed` / `products:zh+products:en`のまま変わっていないので、**同じ出典を読み直した
訂正**に見える。型番の`B`=128K / `C`=256Kという命名規則とも整合する。
480Kは「零等待領域を含む総容量」だった可能性が高い。
**どちらを`upload.maximum_size`にすべきかは判断が要る**(小さいほうが安全側)。

**(b) V303/V305/V307のSPI1既定routeが変わる。**

```
SCK  PB3 → PA5      MISO PB4 → PA6      MOSI PB5 → PA7
REMAP_VAL 1 → 0     ROUTE_COUNT 1 → 2   PIN_SPI_SS PA15 → PA4
```

route数が1→2になり、**reset既定(remap 0)が選べるようになった**ための変化。
生成器はreset既定を最優先する方針なので**この動きは方針どおり**だが、
既存利用者のSPI pinが変わる。

**(c) CH32V205からI2C2が消える。** これは**我々の生成器の問題**。

新表で`CH32V203CCT6`のI2C2 af-7が**2組**になった。

```
PB10 / PB11                 (b1285deからある)
PC13-TAMPER-RTC / PC14-OSC32_IN   (155c398で追加)
```

`load_pin_routes()`は`out[part][(index, route)][role] = pad`で**後勝ちの上書き**
をするので、PB10/PB11がPC13/PC14に潰される。**同じ現象は既に起きていた**
(I2C1 af-7はPA14/PB6/PB8の3候補があり、黙ってPB8が採られている)。
`b1285de`でも起きていた潜在バグが、今回V205で表面化した形。

→ **生成器側で「同じ(instance, route, role)に2つのpad」を衝突として検出すべき**。
黙って最後の1つを採るのは、どちらを採ったか出力から分からない。

なお、上書き後になぜI2C2が**移動ではなく消滅**するのかは追い切れていない。

**(d) 上流へ確認したい1点。** `CH32V203CCT6`のPC13/PC14/PC15に
`I2C2_SCL` / `I2C2_SDA` / `I2C2_SMBA` (af-7)が付いた。PB10/PB11/PB12の
af-7 3点セットと**同じ組み合わせ**で、列ずれの誤読にも見える。
PC13〜PC15はTAMPER-RTC / OSC32のバックアップ領域のpadなので、
I2C2が本当にそこへ出るのかは確認したい。**断定はしない。**

## 5. 変わっていないもの

`CH32V003: clock_init step(s) not emitted: step 6 (trim)` の警告は
**`b1285de`でも出る**。155c398で増えた警告ではない
(以前「新しい警告」と書いたのは誤り)。

---

## 追記: 上流の回答 (2026-08-25)

**(a)(b)(d)は3件とも「上流が正しい」で決着しました。**

### (a) flash — 256Kで正しい

独立した根拠が3つ揃いました。

- `memory_configs.csv`がV307VCT6の**5通りの構成**を持っていて、
  `datasheet_value=1`が256K+64K(素の構成)を指す
- 480Kは`code_flash_bytes`。V317の注記が
  「480Kは領域全体で、設定した分が零等待、残りが非零等待」と言っている
- 同梱の`Link.ld`がV303CB〜V303RB=128K、V307VC/WC/RC・V303VC/RC=288K(最大flash設定)

**480Kは「使えるflashサイズ」ではありませんでした。** 他の分割をmenuで出すなら
`memory_configs.csv`が使えます。

### (b) SPI1 — 正しく、「意見の変更」ではなく「復元」

```
afio-spi1-remap   reset_value=0   valid=0;1
PA4/PA5/PA6/PA7 -> default(remap 0)      PA15/PB3/PB4/PB5 -> remap-1
```

以前はremap-1しか無く、生成器に選択肢がありませんでした。
reset既定が現れたので方針どおりに動いただけです。

### (d) PC13/PC14/PC15のI2C2 — 列ずれではない

```
PC13-TAMPER-RTC   TAMPER/RTC/TIM1_CH4(AF0)/I2C2_SCL(AF7)
PC14-OSC32_IN     OSC32_IN/I2C2_SDA(AF7)
PC15-OSC32_OUT    OSC32_OUT/I2C2_SMBA(AF7)
```

**AF番号はsignal名と同じセルの中に括弧で書かれている**ので、列がずれれば名前ごと
ずれます。しかもp17とp25の**独立した2つの表**に同じ内容で出ます。
こちらの誤読疑いは外れでした。

### (c)の根 — 逆引きは一対多

`(型番, 周辺, 役割, 経路)`に複数padがある組が **984 / 22453 (4.4%)**。

| 経路 | 組数 | 意味 |
|---|---:|---|
| af-N | 860 | 設計どおり。pinごとにAF番号を選ぶので候補は複数 |
| default | 101 | `SYS_NRST`がPA15とPC0(M030)など |
| remap-N | 23 | 本来1組のはず → 3群とも別の話だった |

**AF方式では「衝突」ではなく「選択肢」です。** remap方式は1つのfield値が
pad一式を切り替えるので排他ですが、AF方式はpadごとに独立なので、
同じ機能を出せるpadが並ぶのが正常。
`load_pin_routes()`の後勝ちは、AF方式では**選択肢を黙って1つに潰しています**。
要るのは衝突検出ではなく、**どれを選んだかを出力に残すこと**です
(当初「衝突として落とすべき」と書いたのは誤り)。

### 上流側で見つかった問題 (F-27 / F-28)

`remap-N`の23組を資料へ戻したところ、3群が別方向でした。

| 群 | 判定 |
|---|---|
| CH32V103 TIM3 (18行) | **上流の値が誤り**。RM表10-12は00/10/11でPB4=2・PC6=3 |
| CH32L103 USART2 (35行) | **上流の値が正しい**。RM表10-17の10列がPA11=値2と一致 |
| CH32X033/X035 TIM1 (42行) | **pin表が正しい**。X035のRMに格子の表が無い |

原因は**F-28: CH32L103のremap格子を1行も読めていない**
(`extract_remap.extract()`が`CH32L103RM.PDF`で0行。表10-17〜10-20はp84にある)。
12 familyで0なのは4つ、うちV205/X315(AF方式)とX035(RMに表が無い)は正しく、
**L103だけが取りこぼし**。

**我々の生成物への影響は今のところありません。**

- F-27: タイマのremapを1箇所も使っていない。`load_pwm_pins()`は`default`/`main`
  しか読まず、`load_remap_fields()`はUSART/I2C/SPIだけ。`variants/`にも
  `TIM._REMAP`は無い
- F-28: L103の既定は全て`REMAP_VAL 0`(reset既定)なので出荷経路は格子に依存しない。
  依存するのは`CH32_SERIAL1_ROUTES`のroute 1(PB6/PB7、PCFR1値`0x4`)だけで、
  `setRoute(1)`からしか触れない。`route_selftest`が実機でそこを通って戻って
  きているが、**PB6/PB7には何も繋がっていないのでpinが実際に動いたかは未確認**

---

## 追記2: `944bc9c` の確認 (2026-08-25、同日夜)

F-27/F-28の修正と、こちらが依頼した表の一部が**もう入っている**。
基準線は変わらず(`--tables <b1285de> --check` = DRIFT無し、run-onも4件の誤検出のみで155c398と同一)。

### 直っているもの

- **F-27**: V103 TIM3のremap値が分割された(`PB4`=値2、`PC6`=値3。
  `remap_fields`の`valid_values`から存在しない1が消えた)
- **F-28**: L103のremap格子が読まれるようになった
  (basisが`datasheet-pin-table+rm-remap-grid:en`に。行数207は不変=値は元から正しかった)

### 依頼した表のうち2つが入っている

- **`timers.csv`(64行)** — 依頼した通りのschema
  (`family,timer,kind,counter_width_bits,channels,complementary,update_vector`)。
  **`WIDE_TIMERS`の手書きが消せる**。しかも裏取りが1件正された:
  手書きの「CH32V20xのTIM4は32bit」(EVTヘッダのunionが根拠)は**RM上は16bit**
  (32bitはV205 die側だけ)。seriesでfamilyをunionする実装のため**出力は全部正しかった**が、
  根拠が間違っていた。V203C8T6実機で「16bit部品への32bitストアは無害」を
  確認済みなので、挙動への影響は無し
- **pad正規名の列** — `pin_roles.csv`に`port`/`pin`列が付いた
  (`PC13-TAMPER-RTC,C,13`)。装飾付きpad名のparseが消せる

未着手なのは**既定padの印(preferred)**と**flashの幾何**。

### 生成物への差分: 8 additive / 20 rewriting (155c398の8/15から+5)

155c398からの増分:

- **shared lead対応で新しいpadが増えた**: V004に`PA4`、X035/X033に`PC10`/`PC11`など。
  `pins.csv`が「1本の物理pinを2つのpadが共有する」を表せるようになった
  (X035: pin32=PC11+PC16、pin33=PC10+PC17)。
  **注意**: PC10/PC11はこちらの`UNUSABLE_PADS`
  (`x035-pc10-pc17-bonded`: 駆動するとPC17/PC16も動く)の対象。
  いままで「存在しないpad」だったのが**生成されるpadになる**ので、
  取り込み時にerrataの扱いを見直す必要がある
- **M103のroute表が増えた**(F-28の副産物: 格子が読めた分、SERIAL4/I2C1/SPI1の
  選べるrouteが1つずつ増加)
- **X305のSerial1/I2C1の既定padが動いた**(`PC4/PA12`→`PD4/PD5`)。
  値の訂正ではなく**「表の最後が勝つ」の並び順依存が上流の版間で発現した**もの
  (155c398時点のalternativesコメントに既にPD4/PD5が候補として載っていた)。
  compile onlyのseriesなので実害は無いが、**一対多の選び方を決定的にする件の
  必要性がこれで実証された**——上流が行を並べ替えるだけで既定pinが変わる

155c398で決着済みの(a) flash縮小と(b) SPI1既定route復元はそのまま。
(c) V205のI2C2消滅も変わらず(こちらの生成器の後勝ち問題)。
products.csvの±30行はbasis文字列のみで値の変更なし。

