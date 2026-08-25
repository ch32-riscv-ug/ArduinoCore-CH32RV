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
