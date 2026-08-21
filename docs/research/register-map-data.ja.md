# R-20: レジスタマップを持つとしたら何のデータが要るか(列挙)

日付: 2026-08-21
状態: **列挙のみ。方針は未決定**(ユーザ指示「必要なデータがあればdata側に依頼を出すので列挙だけ先に」)
関連: [R-19](signal-name-normalization.ja.md)、[ADR-0010](../adr/0010-pin-encoding.ja.md)、[todo](../todo.ja.md)

## なにを検討しているか

ESP32やSTM32duinoは、sketchから**レジスタを直接触れる**ヘッダを同梱している。

| コア | 提供物 | 出どころ |
|---|---|---|
| ESP32 | `soc/gpio_reg.h`、`soc/gpio_struct.h` 等 | ESP-IDFの生成物(ベンダ製) |
| STM32duino | `stm32f4xx.h` 等のCMSIS device header | STのCMSISパッケージ(ベンダ製) |
| RP2040 (arduino-pico) | `hardware/regs/*.h` | pico-sdkの生成物(ベンダ製) |
| **このコア** | `cores/arduino/ch32_registers.h` | **手書き。コアが触るものだけ** |

つまりどのコアも「ベンダが配っている機械生成ヘッダをそのまま同梱」しており、
自前で書いているのはうちだけ。WCHの相当物はEVTヘッダだが、
**EVTは参照のみでrepositoryへ取り込まない**という方針([ADR-0003](../adr/0003-owned-startup-vector-linker.ja.md)以来)があるので、
同じ手は使えない。同梱するなら**事実をデータとして持ち直して生成する**ことになる。

現状の`ch32_registers.h`は約300行で、RCC / GPIO / AFIO / USART / I2C / SPI / SysTick / PFIC /
FLASH latency しか無い。sketchがADCのレジスタを直接触りたくなった時点で行き止まりになる。

## 先に確認: 既に存在する機械可読データ

**[ch32-rs/ch32-data](https://github.com/ch32-rs/ch32-data)** が既にある(MIT / Apache-2.0)。
stm32-dataに倣ったYAMLで、MounRiver StudioのSVDを後処理したものと手書き定義の混成。
公開されているfamilyは CH32V0 / CH32V1 / V203・V303 / V305・V307・V317 / V208 / X0 / L1 / CH643 / CH641。

- [ ] **要確認**: このリストにうちの対象のうち **V205 / V407 / V467 / X305 / X315 / M030 / M103 / M007** が無い。
      本当に無いのか、別名で入っているのかを確かめる
- [ ] **要確認**: SVDの出どころ(MounRiver Studio)のライセンス条件。
      ch32-dataがMIT/Apacheで配っていることと、元SVDの条件は別問題
- [ ] **要判断**: ch32-dataを**上流データとして参照する**のか、
      `ch32-device-data`へ**取り込む**のか、独自に集めるのか。
      R-19で「device-dataが正本」の形に寄せたばかりなので、二重管理は避けたい

## device-dataに今あるもの / 無いもの

`tables/`にあるのは products / series / families / packages / pins / pin_functions /
remap_fields / remap_routes / errata / documents / cores / evt_examples /
operating_conditions / product_attributes。

**レジスタに関する表は1つも無い。** `remap_fields.csv`だけが例外的に
「AFIOのどのbitか」を持っているが、これはpin routeのための最小限。

## 依頼するとしたら必要なデータ(D-1〜D-8)

粒度は「**peripheral型 × family**」が正しい。同じI2Cでも
V003/X035にはRTR(rise-time register)が無くV20x/V30xにはある、という差が実在する
(このコアも`CH32_I2C_HAS_RTR`という定数で吸収している)。
型を共有できる単位でまとめないと、11 family × 数十peripheralの総当たりになる。

| # | 欲しいもの | 粒度 | なぜ要るか | 今どうしているか |
|---|---|---|---|---|
| **D-1** | peripheral instance一覧: 名前・instance番号・**base address**・バス(APB1/APB2/AHB) | part または series | どのpartに何個あるか。X035にI2C2が無い類 | 手書き |
| **D-2** | **RCCのクロック許可bit**(register名 + bit位置)、可能ならreset bitも | family | `begin()`が最初に触る。バスとbit位置は別の事実 | 手書き(`CH32_RCC_APB1_I2C1`等) |
| **D-3** | peripheral型ごとの**register一覧**: 名前・offset・幅・access・reset値 | **peripheral型 × 型version** | ヘッダ生成の本体 | 手書き(コアが触る分だけ) |
| **D-4** | register内の**bit field**: 名前・bit位置(範囲)・意味。列挙値があれば列挙値も | 同上 | `CH32_I2C_CTLR1_START`のような定数 | 手書き |
| **D-5** | **peripheral型のversion key**: 「このfamilyのI2Cは型Aだ」と言える識別子 | family × peripheral | D-3/D-4を共有するため。RTRの有無がまさにこれ | `FAMILY`表の`core_defines`に手で1個ずつ足している |
| **D-6** | **割込み番号**とhandler名(instanceごと) | part または family | vector tableとNVIC。**既にEVTから生成している**ので移送候補 | `tools/generate/import_vectors.py`がEVTから生成 |
| **D-7** | **DMA channel対応**(peripheral+方向 → channel) | family | 将来DMAを使うとき。今は未使用 | 無い |
| **D-8** | 各事実の**出典と確信度**(document + 表番号) | 全部 | device-dataの既存慣行に合わせる。PDF由来は誤りが混ざる | remap系は既にこの形 |

### 依頼を出すなら添えるべきこと

- **検証手段**をセットにする。R-19では「EVTの`GPIO_PinRemapConfig`をホストで実行して
  235 selectorを突き合わせる」という独立検証を作れたのが効いた。
  D-3/D-4なら**EVTヘッダの構造体offsetと、生成物のoffsetを突き合わせる**のが同じ役割になる
  (EVTは参照のみ。読んで照合するのは今までどおり可)
- **段階を切る**。全peripheralは大きすぎるので、まず
  **このコアが既に触っている7つ**(RCC / GPIO / AFIO / USART / I2C / SPI / TIM)から。
  次にADC / DMA、その後は必要になった順
- **命名**はreference manual準拠(`CTLR1`/`STAR1`のようにWCH語彙)で揃える。
  ST語彙(`CR1`/`SR1`)へ翻訳しない。翻訳するとPDFと突き合わせられなくなる

## 決めていないこと

- [ ] `[要判断]` **そもそも同梱するか**。sketchがレジスタを直接触るのはArduinoでは逃げ道であって
      主要用途ではない。一方で「ESP32では出来たのに」は実際によく言われる
- [ ] `[要判断]` 同梱するとして**形**: ESP32型(`*_reg.h`のdefine群)か、
      CMSIS型(構造体)か、うちの現行(offsetマクロ)か
- [ ] `[要判断]` **上流をch32-dataにするか、device-dataに持たせるか**
- [ ] `[P2]` D-6(割込み番号)は**うちが既に生成できている**ので、
      公開価値が出た時点でdevice-dataへ移す候補([todo](../todo.ja.md)の同項目)
