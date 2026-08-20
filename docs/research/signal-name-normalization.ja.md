# R-19: device-dataのsignal名正規化と、リマップAPIが必要とするデータ

状態: **調査済み。上流([ch32-device-data](https://github.com/ch32-riscv-ug/ch32-device-data))への依頼を下に用意。作業の深さは判断待ち。**
関連: [Q-011](../open-questions.ja.md)(canonical signalはこのQの review 項目)、
[Q-014](../open-questions.ja.md)、[TODO](../todo.ja.md)、
上流の[extraction-survey](https://github.com/ch32-riscv-ug/ch32-device-data/blob/main/docs/extraction-survey.ja.md)未決定事項#1・#7

## なぜ要るか

リマップの利用者向けAPIは`setPins(tx, rx)`(ピン名)と`setRoute(n)`(ルート番号)を
両方出す方向で検討している(**まだ決定として文書化していない。実装が見えてから
記録する**)。`setPins`は**ピン名からAFIOルート値を逆引きする表**を
生成できることが前提で、その表を作るには「このpadは、このルート値のとき、
どのペリフェラルの何の役割か」が機械可読でなければならない。

役割の判定を誤ると、症状は**Serialの無音の文字化け**になる。ボーレート分周比は
正しく、pinも導通しているのに、TXとRXが入れ替わっている、といった形で出る。
`F_CPU`不一致を`#error`で落とすようにしたのと同じ理由で、ここは生成時に
落としたい。

## 結論を先に

**signal名より、remap fieldの定義のほうが重い。**

`remap_fields.csv`は154 selectorすべてを`register=PCFR1`の単一fieldとして
記録しているが、L103 / M103 / V20x / V30x系では**selectorがPCFR1とPCFR2に
またがる**。そのため16 selectorで`valid_values`が`bits`幅に収まらない。
うちのコアはPCFR1しか書いていないので、これらのseriesでroute 2以上を
使った瞬間に**黙って別のrouteを選ぶ**(F-12〜F-15)。

しかもこちらは**EVTが正確に定義している**ので機械化できる。pad↔役割は
EVTに無い(F-9)のと対照的。

signal名のほうは範囲が狭い。**正規化が要るのはV003 / X033 / X035の3 seriesだけで、
残りは最初からcanonical形**。その3 seriesはdatasheetの一貫した略記法なので、
**辞書ではなく語彙規則で片付く**(263行中260行)。

`candidates/*.json`の`signal_aliases`は使えない。padの一致でaliasを1対1に
決めようとしているが、**1つのpadは複数機能を持つ**ので、その方法では原理的に
決まらない(F-9〜F-11)。

## 確定している事実

計測は`.tools/ch32-device-data/tables`(boards.txtがピン留めしているcommit)、
上流の`candidates/*.json`、およびローカルのEVTミラー12 familyに対して行った。

| # | 事実 | 確認方法 |
|---|---|---|
| F-1 | CH32のリマップは**ペリフェラル単位の番号付きルート**で、TXとRXを独立に選べない。1つのAFIOフィールドが複数の信号を同時に動かす | `remap_routes.csv`を(series, selector, value)で畳むと、1つの値にTX/RX/CK/CTS/RTSが並ぶ |
| F-2 | **selectorからペリフェラルは決まらない。** `afio-spi1-remap`の行に`CAN1_RX`・`TIM3_CH1`・`USART4_RTS`が混ざる | 同上 |
| F-3 | boards.txtが生成する**24 series中19**がAFIO remap方式 | `remap_fields.csv`のseries集合との差 |
| F-4 | 残る5のうち**V205・X305・X315は本当にper-pin AF方式**。EVTヘッダのREMAP定義が`ch32v205.h`で0件、`ch32x3x5.h`で1件 | `grep -c REMAP` |
| F-5 | **V407・V467は抽出漏れ。** `ch32v4x7.h`には**99件**のREMAP定義があるのに、`remap_fields.csv`のselectorは0件 | 同上 |
| F-6 | `remap_routes.csv`は**2228行すべて`confidence: reference`**(最下位) | `confidence`列の集計 |
| F-7 | **既定ルート(value=0)の行が1件も無い** | `value == "0"`の行数 = 0 |
| F-8 | **略記法を使っているのはV003・X033・X035の3 seriesだけ。** 他はすべて`USART1_TX`形式 | signal列を語彙規則で分類し、series別に集計 |
| F-9 | **EVTはpad↔signalをルート値ごとには定義していない。** EVTが持つのはselectorとその値(`GPIO_PartialRemap2_USART2`とビット位置)まで | `ch32x035_gpio.h` / `ch32v30x_gpio.h`の`GPIO_*Remap*`定義 |
| F-10 | EVTのサンプルコードには`USART1_Tx(PA9)`形式のpad注記があり、12 familyで**194件**取れる。役割はTX/RX/CK/CTS/RTS/SCL/SDA/MISO/MOSI/SCK/NSS | EVT全23,672ファイルを正規表現で走査 |
| F-11 | **そのEVT注記でも争点は1件も裁定できない。** 19件中10件はEVTが黙り、8件は「第3の答え」を返す | 下記 |
| F-12 | **16 selectorで`valid_values`が`bits`幅に収まらない。** 例: L103 `USART1_RM`は`bits=2`(1 bit)なのに`valid_values=0;1;2;3;4` | `remap_fields.csv`の各行で`max(valid_values) >= 2**len(bits)`を判定。該当はL103 5、M103 5、V203/V208/V303/V305/V307/V317 各1 |
| F-13 | 理由はEVTにある。**これらのfamilyではremap selectorがPCFR1とPCFR2にまたがる。** `GPIO_PinRemapConfig`に`(GPIO_Remap & 0xC0000000) == 0xC0000000 /* PCFR1 + PCFR2 */`の分岐があり、`GPIO_PartialRemap2_USART1 = 0xD0080000`はPCFR2 bit19 | `ch32l103_gpio.h` / `ch32l103_gpio.c`。PCFR2参照はL103 6件、V20x 3件、V30x 3件、X035とV003は0件 |
| F-14 | `remap_fields.csv`は**154行すべて`register=PCFR1`**。分割fieldを表現する列が無い | `register`列の集計 |
| F-16 | **EVTの`GPIO_PinRemapConfig`をホストでコンパイルして実行すれば、fieldの定義が観測できる。** ENABLEで0から呼べば`set`、DISABLEで全1から呼べば`clear`(=field)が取れる | [tools/generate/evt_remap_fields.py](../../tools/generate/evt_remap_fields.py)。12 familyすべてで動作 |
| F-17 | その結果と`remap_fields.csv`は**129 selectorで一致、12で不一致**。不一致は全てL103/M103で、**表にPCFR2側のbitが無い** | 同toolの`--compare` |
| F-18 | **表に丸ごと無いselectorがある。** X035の`USART1`(PCFR1:5,6)と`TIM1`、X033の`SPI1`/`USART1`/`USART3`、L103/M103の`USART4`、V407/V467は34個すべて。**X035の既定SerialはUSART1** | 同上。EVT 11 selectorに対し表は8 |
| F-19 | 12 familyの`GPIO_PinRemapConfig`は**全て実装が異なる**(共有ゼロ)。だから書き写さず実行する | 関数本体をハッシュして比較 |
| F-15 | うちのコアは**PCFR1しか書いていない**。`CH32_AFIO_PCFR2`は定義済みだが未使用で、`remap_mask_value()`は`bits`に収まらない上位を黙って捨てる | [HardwareSerial.cpp:22](../../cores/arduino/HardwareSerial.cpp#L22)、[ch32_registers.h:90](../../cores/arduino/ch32_registers.h#L90)、`generate.py`の`remap_mask_value` |

## `signal_aliases`が使えない理由

導出は「padとselector値で突き合わせる」方式で、名前を使わない点は正しい。
しかし**1つのpadは複数機能を持つ**ため、その突き合わせから1対1のaliasは
決まらない。重複を除いた187件の内訳:

| 区分 | 件数 | 判定 |
|---|---:|---|
| キーが既にcanonical形 | **100** | **構造的に誤り。** aliasではなく「同じpadを共有する別機能」 |
| キーが略記・語彙規則と一致 | 55 | 妥当 |
| キーが略記・語彙規則と不一致 | 19 | 下記 |
| キーが略記・規則が当たらない | 13 | `AETR2`、`TIETR`、`X`など |

不一致19件をEVTの注記(F-10)で裁定できるか試した結果が**10件が沈黙、
8件が第3の答え**で、その8件はこうなる:

```
CH32X035 T1C1N   表: USART4_CTS   規則: TIM1_CH1N   EVT: SPI1_MOSI
CH32X035 T1C2    表: USART2_RX    規則: TIM1_CH2    EVT: USART1_TX
CH32X035 T2C1N   表: TIM2_C1N     規則: TIM2_CH1N   EVT: USART1_RX
```

三者が食い違っているのではなく、**同じpadに3つとも載っている**だけである。
つまりpad一致は「どの機能か」を決める根拠になりえない。aliasという枠組み自体が
この問題に合っていない。

一方、`T1C1 -> TIM1_CH1`は**pad とは無関係な語彙の対応**であり、datasheetが
series内で一貫した略記法を使っている限り正しい。実際に規則だけで:

| series | 語彙規則が覆う行 |
|---|---|
| CH32X033 | **44 / 44** |
| CH32X035 | **166 / 167** (残り`PIOC_IO0`。これは実在の信号) |
| CH32V003 | **50 / 52** (残り`AETR2`、`TIETR`) |

規則は`TXn/RXn/CKn/CTSn/RTSn -> USARTn_*`(nなしはinstance 1)、
`SCL/SDA -> I2C1_*`、`MISO/MOSI/SCK -> SPI1_*`、`CS -> SPI1_NSS`、
`TxCy / TxCHy -> TIMx_CHy`。

`TIETR`は`T1ETR`の誤植に見える(導出済みaliasも`TIM1_ETR`と対応させている)。
`AETR2`はADCのトリガでペリフェラル役割ではない。

---

## 依頼(ch32-device-data宛)

> ここから下は上流のissueへそのまま貼れる形にしてある。

ArduinoCore-CH32のリマップAPI(`setPins(tx, rx)`)のために、
pin名→AFIOルート値の逆引き表を生成したい。必要なデータを優先度順に挙げる。

### D-0. 分割remap fieldを表現できるようにする(最優先)

`remap_fields.csv`は154行すべて`register=PCFR1`だが、L103 / M103 / V20x / V30x系では
selectorが**PCFR1とPCFR2にまたがる**(F-13)。そのため16 selectorで
`valid_values`が`bits`幅に収まっていない(F-12)。

これは値が欠けているのではなく、**schemaが分割fieldを表現できない**問題に見える。
`register`が1列、`bits`が1つのリストしかないため。

EVTが正確に定義している。`ch32l103_gpio.h`の各`GPIO_*Remap*_*`は
「どのregisterのどのbitに何を書くか」を1語にまとめており、
`ch32l103_gpio.c`の`GPIO_PinRemapConfig`がそれを復号する。
**pad↔役割と違って、ここは機械抽出できる。**

consumer側の影響は具体的で、うちのコアはPCFR1しか書いていない(F-15)。
route 1まではPCFR1で足りるので今は動いているが、route 2以上を使った瞬間に
**黙って別のrouteを選ぶ**。エラーにならないので、症状はSerialの文字化けになる。

#### D-0の作業手順(検証済み)

書き写す必要はない。**EVTの関数をそのまま動かせば取れる。**
動くtoolがこのrepositoryにある。**上流のtree(作業中)には何も置いていない**ので、
手が空いたときに引き取ってもらう形にしてある。

```sh
uv run tools/generate/evt_remap_fields.py --mirrors <EVTクローンの親> \
    --compare <ch32-device-data>/tables
```

上流へ移すなら`tools/extract_remap_fields.py`が`build_remap.py`の隣に並ぶ位置。
**移した時点でこちらのコピーは消す** ——
同じデコーダが2箇所にあると必ずずれるため。

やっていること:

1. `<fam>_gpio.c`から`GPIO_PinRemapConfig`とファイル内`#define`だけを抜き出す
2. `AFIO`を`PCFR1`/`PCFR2`の2語に差し替えたshimと一緒に**ホスト用にgccでコンパイル**
3. `<fam>_gpio.h`の`GPIO_*Remap*_*`定数を1つずつ食わせ、
   - 全0から`ENABLE`で呼ぶ → **set**(その経路が立てるbit)
   - 全1から`DISABLE`で呼ぶ → **clear**(=**fieldそのもの**)
4. `clear`をLSB順に並べたものがbit列、`set & clear`をその順で読んだ整数が値

12 familyは**実装が全部違う**(F-19)ので、書き写す方式は12回間違える機会がある。
実行なら転記が無い。V20xのように`*(uint32_t *)0x40022030`を読む実装があるので、
そのページはharness側でmmapして与え、値を変えて**答えが変わるかどうか**まで見る。

出力例(CH32X035、`表に無い`のは`remap_fields.csv`に該当行が無いもの):

```
  I2C1     PCFR1:2,3,4       値 1..5
  SPI1     PCFR1:0,1         値 1..3
  TIM1     PCFR1:15,16,17    値 1..4      表に無い
  USART1   PCFR1:5,6         値 1..3      表に無い  <- 既定Serialのペリフェラル
  USART2   PCFR1:7,8,9       値 1..4
  USART4   PCFR1:12,13,14    値 1,2,3,4,7  <- 5と6は欠番
```

`valid_values`についても、EVTは**定数として存在する値**を返す(USART4の1,2,3,4,7)。
表のほうはbit幅から機械的に0..7としており、欠番が落ちている。

#### 期待するCSVの形

`bits`にregisterの資格が要る。案:

```
series, selector, controller, register, field, bits, ...
CH32L103, afio-usart1-rm, afio, PCFR1|PCFR2, USART1_RM, PCFR1:2;PCFR2:19;PCFR2:20, ...
```

`bits`をLSB順に`<register>:<bit>`で並べる形なら、既存の`bit_positions`の考え方
(分散fieldをLSB順で表す)をregisterまで広げるだけで済む。
V20xについては「この配置は`0x40022030`の読み値に依存する」ことを
どこかに持てると、consumerが実行時分岐の要否を判断できる。

### D-1. 略記signalの正規化(範囲は狭い)

`remap_routes.csv`の`signal`は原典の表記そのままで、同じ役割が
`USART1_TX` / `TX2` / `UTX`のように書かれる。ただし調べたところ、
**略記を使っているのはV003 / X033 / X035の3 seriesだけ**で、
残りは最初からcanonical形だった。

その3 seriesは datasheet が一貫した略記法を使っているので、**語彙規則で
263行中260行が機械的に決まる**(規則は上に書いた)。残る3行は
`PIOC_IO0`(実在)、`TIETR`(`T1ETR`の誤植に見える)、`AETR2`(ADCトリガ)。

欲しいのは、**verbatimな`signal`を残したまま**正規化された役割が引ける形。
列が分かれていると扱いやすい:

```
series, selector, value, signal, pad, peripheral, role
CH32X035, afio-usart2-remap, 1, TX2, PA20, USART2, TX
CH32V307, afio-usart1-remap, 1, USART1_TX, PB6, USART1, TX
```

1列(`canonical_signal`)でも構わない。**`signal`列の書き換えは望まない** —
原典の表記は出典として価値がある。決められない行は空でよい。埋めるより、
埋まっていないことが分かるほうが重要で、空の行はこちらで生成対象から外す。

### D-2. 既定ルート(value=0)を明示する

`remap_routes.csv`にvalue=0の行が1件も無い(F-7)。既定経路は
`pin_functions.csv`の`route=default`から導けるが、リマップ後の経路と
同じ表に並んでいないと、consumerが2つの表を突き合わせる規則を持つことになる。
上流の未決定事項#7そのもの。

### D-3. `signal_aliases`はエクスポートしないでほしい

導出方法(pad一致)が問題に合っていない。1つのpadは複数機能を持つので、
そこから1対1のaliasは決まらない。実際、重複除去後187件中100件は
キーがcanonical形で構造的に誤り、残る不一致19件はEVTのpad注記でも
1件も裁定できなかった(F-11)。

D-1を語彙規則でやれば`signal_aliases`は要らなくなる。

### D-4. V407 / V467のremap selectorが未抽出(F-5)

`ch32v4x7.h`に99件のREMAP定義があるのに、`remap_fields.csv`のselectorは0件。
V205 / X305 / X315は本当にper-pin AF方式なので対象外でよいが(F-4)、
この2つはsiliconの仕様ではなく抽出の穴に見える。

### こちらから出せる材料

- **語彙規則**(3 seriesで260/263行を覆う)。規則そのものはこのファイルに書いてある
- EVTサンプルから機械抽出した**194件の(peripheral, role, pad)**。ルート値までは
  分からないが、既定経路の裏取りには使える
- **抽出tool本体**([tools/generate/evt_remap_fields.py](../../tools/generate/evt_remap_fields.py))。
  そのまま引き取れる。上流へ入った時点でこちらからは消す。
  **CI依存は増えない**——上流のCIはextractorを1つも実行しておらず、
  `candidates/`の成果物を検証する形なので、gccが要るextractorは
  「手元で回して成果物をコミットする」既存の流れにそのまま収まる

こちら側に残す検査は**表だけを読むもの**にする。「生成したmaskが表のfieldと
一致し、bitを取りこぼしていないか」ならEVTもコンパイラも要らない。
いまEVTデコーダをこちらに持たないのはそのため。

### 優先series

X035が主対象。次いでV003 / V006 / V203 / L103 / V103 / V307。
X033はX035と同じ表を共有する。

---

## 判断ポイント

1. **D-0を先にやるか。** signal名(D-1)より重いと考える。失敗が無音で、
   影響が8 series(V203とV307を含む)に及び、しかもEVTから機械抽出できる
2. D-1を上流でやるか、生成器側の`UART_SIGNAL_RE`を広げて受けるか。
   3 series・260/263行なら**どちらでも現実的**だが、上流でやれば
   他のconsumerも同じ表を使える
3. `TIETR`が`T1ETR`の誤植かどうかは、datasheetの原文確認が要る
4. D-4(V407/V467の抽出漏れ)は、この2 seriesを実際に対応させる時期次第
5. コア側にPCFR2書き込みを足すのは、D-0でデータが入ってからでよいか
   (先に足しても書く値が無い)
6. V20xの`0x40022030`が実機でどちら側かは未確認。V203を載せれば
   `probe-rs read b32 0x40022030`で読める
