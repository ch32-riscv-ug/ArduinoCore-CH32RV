# harness をどう叩くか — テストの駆動方式

文書状態: **提案**(大枠の設計案。実装は未)
文書基準日: 2026-09-06
前提: [harness-probe](harness-probe.ja.md)(採る/採らないの評価)、
[harness-wiring](harness-wiring.ja.md)(どの pad に繋ぐか)。本文書は**その上で誰がどう叩くか**を決める。
関連: [test-strategy](test-strategy.ja.md)(HIL protocol と論理信号名)、
[../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md)(検証方法 1〜4)、
[../tests/manual/README.ja.md](../tests/manual/README.ja.md)(既存の手動テスト)

> **位置づけ**: 本文書は**コア側の立場**であり、protocol 側との合意ではない。
> 最終的な調整は `wch-protocols` で各リポジトリの要望を突き合わせて行われる。
> いまは要求を**広げる**段階なので、ここの「結論」は要求の根拠であって決定ではない。
> 要求そのものは ID 付きで[harness-requirements](harness-requirements.ja.md)に集約した。

---

## 0. 結論(先に)

1. **主人公は pytest。DUT も harness も従。** 現状(`smoke.py` / `reg_probe.py`)の素直な延長で、新しい判断ではない。
2. **「probe は能力を宣言するだけ。何に使うかは感知しない」で正しい。**
   ただし宣言だけでは束縛が決まらない。**4 つ目の入力**が要り、それは配線表でもテストでもなく
   **DUT 側の pinmux データ**(`ch32-device-data`)。この 4 つを resolver が突き合わせる(§1)。
3. **叩き方は「CLI を毎回起動」でも「アプリ経由」でもなく、セッション。**
   根拠は手元にある — **`reg_probe` は 1 レジスタ読むごとに 1 プロセス起動していて**
   (`Reader.words()` が `subprocess.run`)、**1 board 30〜130 秒**の大半がそれ。
   200〜400 項目 × プロセス起動が支配的で、セッションなら往復は ms になる。
4. **transport は最初から差し替え可能にし、`socket://` を第一級にする**(§3)。
   実機なし mock・遠隔ベンチ・pytest-embedded の既存 idiom の 3 つが同時に効く。
5. **DUT の制御チャネルを UART から DMI(`SerialRTT` / `SerialDMDATA`)へ移す**(§4)。
   いま `reg_probe` は pyserial で DUT の UART を掴んでいるので、**その USART は試験対象にできない**。
   V003 は USART が 1 本しかない。
6. **配線は利用者の責任、で正しい。ただし「配線されていない」は skip であって pass ではない。**
   ベンチが何を実行できるかを**カバレッジとして報告する**仕組みが要る(§8)。これが無いと
   「配線は利用者の責任」が「静かに何も試験していない」に化ける。
7. **デバイス模型は probe 側に埋め込み、host は「どれをどのバスに繋ぐか」を選ぶだけにする**(§9)。
   **転送のたびに host へ取りに行く形は採らない** — SPI と UART は待たせる手段が無く原理的に不可、
   I2C は clock stretch で成立はするが**実物の 20 倍遅い相手**を DUT に見せることになり、
   タイミング測定も再現性も壊れる。
   ただし **preload / 走行後の readback / 低頻度の注入は host で構わない**。
   EmbedBench の IF がその分担で凍結されている(`reset` / `channelWrite` / `channelRead` / `dump` は
   effect-free と契約されていて critical path の外にある)。

---

## 1. 誰が何を持つか — 4 つの宣言と resolver

### 1.1 4 つの独立した宣言

| 宣言 | 誰が書くか | 中身 | 例 |
|---|---|---|---|
| **probe capability** | **probe firmware が実行時に申告** | channel ごとに取れる機能、排他グループ、capture unit、時間軸の確度 | `ch4: [gpio, capture, spi0.rx, i2c0.sda, uart1.tx, pwm2a]` |
| **配線表** | **人**(ベンチごと。治具に付属させてもよい) | probe channel ↔ DUT pad、電圧、pull-up の所在、直列抵抗 | `ch4 -> PA7` |
| **DUT の pinmux** | `ch32-device-data` から生成 | pad ↔ (peripheral, role, route) | `PA7: SPI1.MOSI(default) / USART1.RX(remap-3) / ADC IN7 / TIM3.CH2` |
| **テストの要求** | テスト作者 | **役割**と、DUT 側のどのペリフェラルに繋ぐか | 「`i2c_target(0x50)` を DUT の `Wire` に」 |

### 1.2 resolver が束縛を作る

```text
テストの要求 ─┐
probe caps  ─┼─→ resolver ─→ 束縛(binding)
配線表      ─┤                  ├─→ harness へ: channel に機能を割り当てる
DUT pinmux  ─┘                  └─→ DUT agent へ: どの route を選ぶか
```

**この形にすると、テストに pin 番号も route 番号も出てこない。**
probe は「何に使われるか」を知らないまま、host が自分の都合で機能を割り当てる。

そしてもう 1 つ効果がある。[harness-wiring](harness-wiring.ja.md) §8 の
「配線済みの pad へ DUT 側を `setRoute()` で寄せる」を、**人が表を睨んでやらずに済む**。
resolver が pinmux DB を引いて route を選ぶ。

> **データは揃っている。** `ch32-device-data` の `index/pinout.csv` は
> **24 series 全部**(`evidence/remap_routes.csv` に route が無い V003 / V205 / X305 / X315 を含む)を
> part number 別・全 route で持っている。resolver に必要なものは今日ある。

### 1.3 束縛が決まらないときの振る舞い

コアの既定([upload-and-fixture](upload-and-fixture.ja.md))は
**「複数候補が残る場合は書き込みを中止する」**で、resolver も同じにする。

| 状況 | 振る舞い |
|---|---|
| 要求を満たす束縛が無い | **skip**。ただし**何が足りないかを出す**(§8) |
| 束縛が 2 通り以上ある | **fail closed**。テストか配線表で一意にする |
| 配線表と caps が矛盾(存在しない channel) | **fail**。ベンチの設定ミス |
| 束縛はできたが probe が `configure` を拒否 | **fail**。caps の宣言と実装が食い違っている |

---

## 2. probe に何を宣言してもらうか

`caps` の語彙は **probe 側の言葉だけ**にする。`MARKER` も `UART_TX` も
`CH32V307` も出てこない。形は案:

```jsonc
{
  "protocol": "harness/1",
  "firmware": "0.1.0",
  "serial": "E6614C311B7A2C31",          // 個体識別(harness-probe 5-6a)
  "timebase": { "source": "xtal", "ppm": 30, "capture_hz_max": 100000000 },
  "channels": [
    { "id": 0, "functions": ["gpio_in","gpio_out","capture","uart0.tx","i2c0.sda","spi0.rx","pwm0a"] },
    { "id": 1, "functions": ["gpio_in","gpio_out","capture","uart0.rx","i2c0.scl","spi0.cs","pwm0b"] }
    // ...
  ],
  "groups": [
    { "id": "uart0", "kind": "uart",       "roles": ["tx","rx"] },
    { "id": "i2c0",  "kind": "i2c_target", "roles": ["sda","scl"], "addresses": 1 },
    { "id": "spi0",  "kind": "spi_target", "roles": ["rx","cs","sck","tx"], "contiguous": true }
  ],
  "capture": { "units": 1, "width": 16, "base": 0, "contiguous": true, "depth_bytes": 200000 },
  "analog":  { "in": [26,27,28,29], "out": [] },
  "target_writes_on_attach": []            // harness-probe 5-1
}
```

**守ってほしい規則**:

- **channel は番号で、意味を持たない。** 論理名は host 側の辞書。
- **排他と形の制約を宣言する。**「`i2c0` は 1 アドレス」「`spi0` は 4 本連続」
  「capture は base から連番」。**host はこれだけを見て組み合わせを決める**。
- **数値はすべて `caps` が言う。** 上限をプロトコルに焼かない
  (`dmi-bridge` の設計原則 2 と同じ)。
- **`configure` は望む状態を一度に受け取り、成立しなければ丸ごと拒否する。**
  部分適用しない。これが無いと「半分だけ設定された harness」でテストが走る。
- **`caps` に嘘があったら `configure` で落ちる**、という関係にしておく。

---

## 3. 叩き方 — CLI / セッション / socket

### 3.1 CLI とセッションは用途が違う。両方要る

| 方式 | 向く用途 | 向かない用途 |
|---|---|---|
| **CLI 1 回 1 操作**(ch32rv 型) | `caps` / `list` / `doctor` / `selftest`、CI の preflight、人が手で確かめるとき | **テスト本体** |
| **セッション** | テスト本体(arm → RUN → capture → 読み出し) | 単発の診断 |

**根拠(実測済み)**: `reg_probe.py` の `Reader` は
「Memory reads through the WCH-Link, **one process per read**」と自分で書いていて、
`self.calls` と `self.seconds` を数えている。**1 board あたり 200〜400 項目で 30〜130 秒**。
`ch32rv` に替えて 5 倍速くなったと注記があるが、**それでもプロセス起動が支配的**。

> テスト本体を CLI で叩く設計は、**この計測でもう否定されている**。

### 3.2 transport は差し替え可能にする

| transport | 用途 |
|---|---|
| `serial://`(probe の CDC) | 既定。追加の常駐なし |
| `usb://`(vendor bulk) | capture の帯域が要るとき |
| **`socket://host:port`** | **遠隔ベンチ / mock** |

**`socket://` を第一級にする理由は 3 つあり、3 つ目が本命**:

1. **遠隔ベンチ**。[TEST_PLAN](../tests/TEST_PLAN.ja.md) の「案 b: 別ホストを runner にして board farm」が
   そのまま成立する。
2. **pytest-embedded の既存 idiom**。EmbedBench は `sketch.yaml` の
   `port: socket://localhost` で既にこれを使っている。
3. **実機なしの mock が同じ口に挿さる**。テストコード自体を CI で回せるようになる(§7)。

### 3.3 client library と CLI は 1 つの protocol を共有する

- **wire protocol は 1 つ**。CLI はその薄いラッパ。
- **exit code と JSON envelope は ch32rv の contract に合わせる**([harness-probe](harness-probe.ja.md) の依頼 5-6e)。
  ベンチのスクリプトが 2 つ目の方言を覚えなくて済む。
- **Arduino 専用にしない**。コアの既定方針([upload-and-fixture](upload-and-fixture.ja.md))どおり、
  `ch32fun` 等からも同じ CLI/API で使えること。

---

## 4. DUT 側 — 「アプリ経由」は 2 通りあり、両方要る

### 4.1 passive と agent

| 方式 | スケッチ | 何が分かるか | 何が分からないか |
|---|---|---|---|
| **passive** | **利用者が書くのと同じ普通のスケッチ**。テスト用の細工なし | 「例が本当に動く」。忠実度が最も高い | DUT が何を受け取ったか(harness の観測だけが頼り) |
| **agent** | host が API を名指しで呼ばせる遠隔操作スケッチ(`reg_probe` の `TESTCMD` 方式) | API の conformance、境界値、戻り値 | 利用者の書き方で動くか |

**両方要る**。コアは既に両方を持っている(`tests/sketches/` が前者、`manual/reg_probe/` が後者)。

### 4.2 agent の制御チャネルを UART から外す(重要)

いま `reg_probe.py` は **pyserial で DUT の UART を掴む**。その結果:

- **掴んだ USART は試験対象にできない。** V003 は USART が 1 本しかない。
- **X033 は `USART1` の既定 route が `I2C1` と同じ pad**([harness-wiring](harness-wiring.ja.md) §9-4)。
- upload 直後に **LinkE の CDC 配送が止まる**実測があり、port を開き直して回避している。
- **往復が遅い**(初代 Link で `PING`→`PONG` 約 5 秒)。

harness は DMI を持つので、**制御チャネルを `SerialRTT` / `SerialDMDATA` に移せる**。

| | 効果 |
|---|---|
| USART が全部空く | [harness-wiring](harness-wiring.ja.md) §5.2 のとおり、全インスタンスを試験に回せる |
| CDC 配送の癖を踏まない | flash 直後の無音・行の途中で止まる、が消える |
| 往復が速い | DMI 経由の polling。UART bridge を経由しない |
| V003 でも成立する | 制御に pad を 1 本も使わない |

**コア側の前提は既に揃っている** — `SerialRTT` / `SerialDMDATA` は実装済みで、V003 実機で
双方向を確認済み([peripheral-support](peripheral-support.ja.md))。足りないのは
**probe 側がこの経路を native に扱うこと**([harness-probe](harness-probe.ja.md) の依頼 5-9)。

### 4.3 生成ヘッダはほとんど要らなくなる

いま `tests/manual/env_config.py` は「pin は Arduino では**コンパイル時**の値なので、
実行時に serial で渡せない」ため、ビルド前に `env_config.h` を書き出している。

**agent が実行時に pin と route を受け取れば、この生成ヘッダは多くのケースで不要になる。**
`reg_probe` の `TESTCMD` は既にそうなっていて、pin を**ポート符号化した数値**でコマンドに載せている。
`SoftSPI(sck, mosi, miso)` のようにコンストラクタ引数の実装も実行時でよい。

残るのは本当にコンパイル時定数が要るものだけ(ビルドオプション、`#if` で切る機能)。

---

## 5. テストの見え方

resolver が効いていると、テストにはこう書ける(案)。

```python
def test_wire_writes_to_eeprom(bench):
    with bench.session() as s:
        # 役割で要求する。channel も pad も route も書かない
        eeprom = s.i2c_target(address=0x50, model="24C02")
        s.capture(["I2C_SCL", "I2C_SDA"], trigger="marker", pre_us=200)

        dut = s.dut_agent()                 # 制御は DMI 経由
        dut.wire_begin(bus="I2C1")          # resolver が route を選び、setRoute を送る
        dut.wire_write(0x50, b"\x00hello")

        ev = s.events()                     # 1 行 1 イベント、同一時間軸

    assert eeprom.memory[0:5] == b"hello"           # 相手が見たもの
    assert [e.kind for e in ev.i2c] == ["start", "addr_w", "data", "data", "stop"]
    assert 90_000 <= ev.i2c.clock_hz <= 100_000     # timing は別レイヤの判定
```

束縛できないときは、**次の一手が書いてあるメッセージで skip する**:

```text
SKIPPED: i2c_target を DUT の I2C1 に束縛できない
  I2C1 は SCL=PB6 SDA=PB7(default)/ PB8 PB9(remap-1)に出せる
  配線表にあるのは PA2 PA3 PA4 PA5 PA6 PA7 PA9 PA10 PB0 PB1 PA13 PA14
  → PB7 を ch2/ch6/ch10/ch14 のどれか(i2c1.sda)、PB6 を ch3/ch7/ch11/ch15(i2c1.scl)へ配線する
```

---

## 6. 既存資産への載せ方

**作り直すものはほとんど無い。**

| 既存 | どうなるか |
|---|---|
| `tests/manual/conftest.py` の `bench` fixture | 配線表と caps も読むように拡張。probe 選択(`smoke.resolve_bench`)はそのまま |
| `tests/manual/bench.json` | **配線表の置き場所になる**。いまは USART route の上書きだけで中身が空 |
| `tests/manual/env_config.py` | agent が実行時に受け取れば、多くのケースで不要(§4.3) |
| `reg_probe.py` の `Reader` 抽象 | **`HarnessReader` を足すだけ**。既に `ProbeRsReader` / `Ch32rvReader` の 2 実装がある形 |
| `reg_probe` の `TESTCMD` | **agent protocol の原型**。作り直さず拡張する |
| `Check` / `Report` / `Tables` | device-data から期待値を組む仕組みはそのまま使える |
| `pytest-embedded` | `socket://` 経路を既に扱える(EmbedBench の実績) |

**`Reader` が既に「バックエンド差し替え可能」になっているのは幸運**で、
harness を 3 つ目の実装として足せば、`reg_probe` の 200〜400 項目がそのまま速くなる。

---

## 7. 実機なしで回す(mock harness)

`socket://` を第一級にする本命の理由。mock を 3 段階で持つ。

| 段 | mock が返すもの | 何をテストできるか |
|---|---|---|
| 1 | **`caps` だけ** | resolver。配線表の誤りと、テストの要求が満たせるかを**実機なしで CI 検出** |
| 2 | **論理的な応答**(バスのやり取り、イベント列) | テストのロジック。**ここに EmbedBench が入る**([harness-probe](harness-probe.ja.md) §6) |
| 3 | **保存した capture の replay** | decoder の回帰([test-strategy](test-strategy.ja.md) の replay 層) |

これで **テストコード自体が CI でテストされる**ようになる。
いま `tests/manual/` は「人が実行する層」で、**そこにあるコードは誰も検証していない**
(`norecursedirs` に入っているので `pytest` を素で回しても走らない)。

段 2 で EmbedBench を使うと、[harness-probe](harness-probe.ja.md) §6 の
「同じ模型を host と実機で走らせて diff する」が**実装形に落ちる** —
mock と実機が**同じ socket protocol** を喋るなら、テストは 1 本でよい。

---

## 8. 失敗と skip、そしてカバレッジ報告

### 8.1 何を skip にし、何を fail にするか

コアの既定([test-strategy](test-strategy.ja.md))は
**「fixture health の失敗と core の回帰を別の結果にする」**。resolver にも同じ線を引く。

| 状況 | 結果 |
|---|---|
| 配線表にその pad が無い | **skip**(このベンチは配線されていない) |
| probe の caps が足りない | **skip** |
| 束縛はできたのに DUT が応答しない | **fail**(core の回帰) |
| 束縛が曖昧 | **fail closed** |
| 配線表と caps が矛盾 | **fail**(ベンチ設定のミス) |

### 8.2 「配線は利用者の責任」を運用可能にする唯一の仕掛け

skip が静かに増えると、**何も試験していないのに緑になる**。
だから **ベンチのカバレッジを報告する**:

```text
bench: V307 / harness E6614C311B7A2C31 / wiring: bench-01.yaml
  実行   38 / 52 cases
  skip   14
    SPI2 系      6 件   PB13 PB14 PB15 が未配線
    USART5/6/8 系 5 件   PB4 PB5 PB8 PB9 PC10 PC11 が未配線
    DAC 系       3 件   ch26/ch27 のアナログタップが未配線
```

これは **CI の成果物**にする。[test-strategy](test-strategy.ja.md) の artifact 一覧
(build metadata・fixture ID・analyzer 設定)に「**その run が実際に何を検証したか**」を足す形。

---

## 9. ベンチとのつなぎこみ — デバイス模型はどこで動くか

### 9.1 ベンチが宣言するもの(能力の出どころは 2 つある)

ベンチは配線表だけではない。**能力は harness と物理ベンチの両方から出てくる**。

| 出どころ | 宣言するもの | 例 |
|---|---|---|
| **harness firmware** | エミュレートできる**器**と**模型**(名前 + 版) | `regfile` / `blockdev` / `bytestream`、`model:24c02@1`、`model:sdcard@2` |
| **物理ベンチ** | 実際に載っている**本物**のデバイス、pull-up、電圧、電源制御 | `real:at24c02 @0x50`、`pullup: bench 4.7k`、`vdd: 3.3V switched` |
| 配線表 | probe channel ↔ DUT pad | `ch4 -> PA7` |
| DUT | board / part number | `CH32V307VCT6` |

**テストからは同じ語彙で見える。**「I2C に 24C02 がいる」だけでよく、
それがエミュか実物かをテストが気にする必要はない
(気にしたいテストのために、束縛の結果には出どころを含める)。

### 9.2 host からスタブで転送を返すのは、やはり辛い(数字で)

| バス | 1 byte の時間 | host 往復 | 待たせる手段 | 成立するか |
|---|---|---|---|---|
| I2C 100 kHz | 90 µs | 0.5〜2 ms(USB CDC) | **clock stretch** | △ 成立はする。ただし遅い |
| I2C 400 kHz | 22.5 µs | 同 | clock stretch | △ 比率はさらに悪い |
| SPI 1 MHz | 8 µs | 同 | **無い**(controller が clock を出す) | ✕ |
| UART 115200 | 87 µs | 同 | **無い**(既定でフロー制御なし) | ✕ |

I2C の余裕は測れる。コアの `Wire` は **1 回の待ちに 25 ms の上限**
(`CH32_WIRE_TIMEOUT_US`、AVR と違って**既定で ON**)。
1 byte あたり 2 ms の stretch なら上限は踏まないが、**10 byte の転送が 1 ms → 20 ms になる**。
実物の 20 倍遅い相手を DUT に見せることになるので:

- **タイミングの測定(方法4 の目的)が意味を失う**。
- host 側のジッタ(USB フレーム 1 ms、OS のスケジューリング)がそのまま応答時間に乗るので、
  **同じ入力で同じ結果にならない**。コアの再現性の要求と噛み合わない。

> **結論: 転送のたびに host へ取りに行く設計は採らない。** ユーザの直感どおり。

### 9.3 3 段に分ける

| 段 | どこで動くか | 何に使うか | firmware 更新 |
|---|---|---|---|
| **器(汎用)** | probe。**host が中身を流し込む** | register file / block device / byte stream。24Cxx、汎用 reg-map センサ、SPI NOR | **不要**(パラメータだけ) |
| **模型(組み込み)** | probe。**コードごと firmware に入る** | 状態機械が要るもの: SD カード、Modbus、NMEA、AT モデム、わざと壊れる部品 | **要る** |
| host stub | host | **転送の critical path には置かない。** 事前ロード・走行後の読み出し・低頻度の注入だけ | 不要 |

**器で足りるならそれが最良**(firmware を触らずにテストを増やせる)。足りないものだけ模型にする。
`dut-harness-design.ja.md` §6 の「probe は器だけ持つ」は**原則として正しく、
本文書はその例外をどこに置くかを決めている**。

### 9.4 EmbedBench の IF が、そのまま「host が選ぶ / probe が動かす」の境界になる

凍結済みの `ebdev::Device` は、まさにこの分担で設計されている。

| 誰が | いつ | EmbedBench の IF |
|---|---|---|
| **host** | 走行前 | `reset()` → `channelWrite()` で初期状態を流し込む |
| **probe(模型)** | **走行中** | `i2cWrite` / `i2cRead` / `spiTransfer` / `serialIn` / `lineIn` / `advanceTo` — **host は関与しない** |
| host | 走行中(低頻度) | `channelWrite()` で環境量を注入(「温度を 30 ℃ に」) |
| host | 走行後 | `channelRead()` / `dump()` で「相手が何を見たか」を回収 |

**`reset` / `channelRead` / `dump` は effect-free(`HostPort` を呼んではならない)と契約で決まっている**ので、
**host が任意のタイミングで直接呼んでよい**。
つまり **preload と readback は最初から critical path の外**にある。
IF がこの分担を想定して凍結されているのは偶然ではなく、`docs/DEVICE_IF_SCOPE.ja.md` の設計そのもの。

模型は純粋 C++11・動的確保なし・22 種で 3572 行なので **RP2040 に余裕で載る**
([harness-probe](harness-probe.ja.md) §6.2)。

### 9.5 firmware に埋め込む代償と、その扱い

| 代償 | 扱い |
|---|---|
| 模型を足すと harness を焼き直す | UF2 で全 OS から更新できる。ベンチ側の作業は数十秒 |
| ベンチごとに載っている模型が違いうる | **`caps` が模型の名前と版を申告する**。テストは名前で要求し、無ければ **skip(理由つき)** |
| 模型の版とテストの期待がずれる | 名前に版を含める(`model:sdcard@2`)。**版が違えば fail closed** |
| RAM / flash を食う | 全部載せる必要はない。**ベンチのプロファイルとしてビルド時に選ぶ** (`probe-pattern-coexistence` の R1「能力は build 時に決まり、役割は実行時に決まる」と同じ) |

### 9.6 まとめ — 選ぶのは host、知らないのは probe

```text
host(pytest)
  ├ 模型を選ぶ           "model:24c02@1 を DUT の I2C1 に、アドレス 0x50 で"
  ├ 初期状態を流し込む    reset / channelWrite
  ├ 走行中に低頻度で注入  channelWrite(「温度 = 30 ℃」)
  └ 走行後に回収          channelRead / dump / イベント列

probe(harness)
  ├ caps       「器はこれ、模型はこれとこれ、版はこれ」
  ├ configure  選ばれた模型を、選ばれた channel/group に束縛する
  └ 走行中     自分で応答する。host に訊かない
```

**probe は「24C02 とは何か」を知っている**(模型のコードを持っている)が、
**「それが DUT のどのペリフェラルに繋がっているか」は知らない**。
束縛は host が決め、probe は channel と group の番号で受け取る。

---

## 10. probe への追加の依頼事項

[harness-probe](harness-probe.ja.md) §5 の 9 件に加えて:

| # | 依頼 | 根拠 |
|---|---|---|
| **5-10** | **`caps` を §2 の形で出す**。channel は番号のみ、機能集合・排他・capture の形・timebase を申告する。**論理名(`MARKER` 等)を probe 側の語彙に入れない** | 論理名は host の辞書。probe が知ると、テストの都合が firmware に染み出す |
| **5-11** | **`configure` を原子的にする。** 望む状態を一度に受け取り、成立しなければ丸ごと拒否(部分適用しない) | 半分だけ設定された harness でテストが走ると、失敗が harness 由来か core 由来か分からなくなる |
| **5-12** | **セッション transport を最初から抽象化し、`socket://` を第一級で持つ。** CLI とライブラリは同じ wire protocol を共有する | §3。CLI 毎回起動はコア側の実測(`reg_probe` 30〜130 秒)で既に否定されている |
| **5-13** | **mock/参照実装を protocol と一緒に出す**(`caps` だけ返す最小のものでよい) | protocol を 1 つに保つ。§7 の段 1 が実機なし CI の入口になる |
| **5-14** | **capture のストリームと control を別チャネルにする** | `probe-pattern-coexistence` §3.2 の R2 と同じ。control の応答性を capture が食わないため |
| **5-15** | **`caps` に「器」と「組み込み模型(名前 + 版)」を並べて申告する**。模型は probe 内で完結して応答し、**転送ごとに host へ問い合わせない** | §9.2 の実測見積り。SPI と UART は待たせる手段が無いので host backed が原理的に成立しない |
| **5-16** | **模型の preload / inject / readback を、転送とは別の経路で受ける**(EmbedBench の `reset` / `channelWrite` / `channelRead` / `dump` に対応する形) | §9.4。effect-free と契約されている経路なので、critical path の外に置ける |

---

## 11. 未決

1. **配線表の書式と置き場所**。`bench.json` を拡張するか、別ファイルにするか。
   ローカル環境固有の値を公開 repository に commit しない規則([upload-and-fixture](upload-and-fixture.ja.md))との整合も要る。
2. **役割の語彙の正本**(`i2c_target` / `uart_peer` / `spi_target` / `gpio_out` / `analog_in` …)。
   probe 側の `caps` とは別の辞書なので、**どちらの repository が持つか**。
3. **resolver の置き場所**。device-data と `boards.txt`/variant の両方を引くので、
   当面はコアの `tests/` に置き、安定したら切り出す、が素直か。
4. **agent protocol を `reg_probe` から切り出すか。**
   いまは 1 スケッチの中のコマンド表。共通化すると `tests/sketches/` からも使える。
5. **セッション中の排他**。DUT の flash を harness がやるのか ch32rv が別にやるのか。
   後者なら**セッションを一度閉じる**必要がある(advisory lock、[harness-probe](harness-probe.ja.md) の依頼 5-6c)。
6. **passive スケッチの観測だけで足りる範囲**。agent を使わずに済むケースを増やせるか。
7. **`socket://` の認証**。遠隔ベンチにするなら要るが、当面 localhost だけなら不要。先送りしてよいか。
8. **どの模型を harness の既定プロファイルに入れるか。** 22 種を全部載せるのか、
   コアの試験に要るものだけにするのか。ビルド時プロファイルの粒度と合わせて決める(§9.5)。
9. **実物デバイスとエミュ模型の使い分け。** 同じ `24c02` がベンチに実物としても載っている場合、
   どちらを既定にするか。エミュは障害注入ができ、実物は忠実度が高い(§9.1)。
10. **模型の版付け規則**(`model:<name>@<ver>`)。EmbedBench の模型に版の概念がまだ無いので、
   どちらの repository が採番するか。
