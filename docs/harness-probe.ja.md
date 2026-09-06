# 自作 probe(DUT harness)の評価と依頼事項

文書状態: **提案**(調査結果と依頼案。個々の実施可否・順序の判断は probe 側)
文書基準日: 2026-09-06
関連: [test-strategy](test-strategy.ja.md)(HIL とロジアナ)、[upload-and-fixture](upload-and-fixture.ja.md)(fixture 構成)、
[ch32rv-requests](ch32rv-requests.ja.md)(同梱 uploader への依頼。本文書はその隣に立つ)、
[../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md)(検証方法 1〜4)

対象の外部 repository(パスは開発環境のもの。link にはしない):

- `../../wch-protocols/` — 線層・USB 層の解読と **harness の設計メモ**
- `../../ch32rv-probe/` — 実装の置き場(2026-09-04 時点で `LICENSE` のみ)
- `../../ch32rv/` — 同梱 uploader。**`DtmAccess` の trait 境界(実在)**。要望は `docs/data-requests/0006-harness-integration.ja.md`
- `../../ch32-device-data/` — device DB
- `../../../dev/EmbedBench/` — host 検証ライブラリ。デバイス IF 凍結済み

> **位置づけ**: 本文書は**コア側の立場**であり、protocol 側との合意ではない。
> 最終的な調整は `wch-protocols` で各リポジトリの要望を突き合わせて行われる。
> いまは要求を**広げる**段階なので、ここの「結論」は要求の根拠であって決定ではない。
> 要求そのものは ID 付きで[harness-requirements](harness-requirements.ja.md)に集約した。

---

## 0. 結論(先に)

1. **使える。しかも「あったら便利」ではなく、コア側で今いちばん詰まっている場所に正面から当たる。**
   [TEST_PLAN](../tests/TEST_PLAN.ja.md) の**方法4(ロジックアナライザ)は全項目が ⬜ で、着手すらできていない**。
   理由は機材ではなく「16ch LA + connector 治具 + 刺激用 controller」を**まとめて決めないと 1 項目も進まない**から
   (Q-050、P0)。harness はこの 3 つを 1 枚に畳む。
2. **ただし価値の順番がコア側と probe 側で違う。** probe 側の作業順は
   `probe-pattern-coexistence.ja.md` §5 が `W(書込) → L(LA) → WL → PIO I2C spike → WLE` としている。
   コア側の困り具合で並べ替えると **`L` と `エミュ(I2C slave)` が先で、`W`(書込)は最後**になる。
   書込は ch32rv + LinkE で既に回っていて困っていない。困っているのは**波形が見えないこと**と
   **相手役がいないこと**。ここは突き合わせたい(§7-1)。
3. **コアは当面これに依存しない形で採用できる。** LinkE で焼いたまま harness は観測と刺激だけをする構成
   (`L` / `WLE` の lane なし)なら、**リリース経路にリスクをゼロで足せる**。
   逆に**同梱アップローダを ch32rv 一本化する決定([ch32rv-requests](ch32rv-requests.ja.md))は動かさない**。
   probe を 2 本目の同梱依存にするのは 1.0 前には勧めない。
4. **いちばん大きい単独の勝ち筋は「target を汚さない attach」**。
   LinkE は attach するたびに target の `RCC_CFGR0` と `FLASH ACTLR` を書き換える(実測)。
   自作 probe は**書かない選択ができる**。しかも debug 線の自己観測で「線に何も出していない」ことを
   **自分で証明できる**。これは host 側のツールをいくら直しても解決しない。
5. **EmbedBench との接続に、まだ誰も書いていない大きな伸びしろがある**(§6)。
   凍結済みデバイス IF は純粋 C++11・環境実装2種。**harness を 3 つ目の環境にすると、
   同じデバイス模型が「host の仮想時計の上」と「本物の線の上」で走る**。
   ログを行単位で diff すれば、**API 契約と実機の食い違いだけが差分に残る**。
   これは host 単体でも実機単体でも作れない種類のテスト。

---

## 1. いま何があるか(4 つの repository の現在地)

| repository | 状態 | 本文書に効く事実 |
|---|---|---|
| **wch-protocols** | 設計済み | 線層・USB 層とも解読がほぼ済み、**harness の設計メモまで書けている**(`dut-harness-design.ja.md` = ピン割当・cross-domain trigger・障害注入、`harness-board-survey.ja.md` = 8 軸の board 比較・RP2350 errata E9、`probe-pattern-coexistence.ja.md` = W/WM/WL/WLE/L/M の共存と作る順序)。**構想側でこちらから足すことはほぼ無い** |
| **ch32rv-probe** | repo のみ | `LICENSE` だけ(2026-09-04 初回 commit)。実装の置き場は確保済み |
| **ch32rv** | 受け口は**片方だけ実在** | probe 経路 verified。**コードにあるのは `DtmAccess`(`dmi_read`/`dmi_write`/`dmi_nop`)だけで、`ProbeService` は architecture の将来計画**(ch32rv `0006` §4.1 の訂正)。`ch32rv-probe-<name>` の枠は P2 で予約済み |
| **ArduinoCore-CH32** | 方法4 が空白 | 方法1〜3 は実装済み(`reg_probe` は 5 枚で 200〜400 項目を配線ゼロで照合)。**方法4(波形)は全項目 ⬜**。入力側 API の刺激手段が無い |
| **EmbedBench** | IF 凍結済み | デバイス IF v1 / rev004(2026-09-04)、模型 22 種・純粋 C++11、環境実装 2 種(host / 純ネイティブ 290 行)。「物理層・波形・サイクル精度」を**明示的に範囲外**とし、越える要望は**実機テストへ振り分ける**と書いてある |

**構造的に噛み合っている点**: EmbedBench が「ここから先は実機」と線を引いた場所と、
ArduinoCore-CH32 が「ここから先は方法4」と書いて空白のままにしている場所が、**同じ場所**である。
harness はその空白そのものを埋める道具になる。

---

## 2. コア側の穴と、harness のどの機能が当たるか

`⬜` = 未着手、`🔧` = 部分的。出典は [TEST_PLAN](../tests/TEST_PLAN.ja.md) /
[test-strategy](test-strategy.ja.md) / [upload-and-fixture](upload-and-fixture.ja.md)。

### 2.1 波形が見えないので止まっているもの(方法4)

| コアの項目 | 現状 | harness の当たり方 |
|---|---|---|
| Serial ボーレート精度 | ⬜ | キャプチャ。**コアは既定で HSI 駆動**(PLL/HSE は将来)なので、これは実質 **HSI トリム実測**。まだ誰も測っていない |
| `analogWrite` PWM 周波数 | ⬜(duty はレジスタで確認済み) | キャプチャ + 周期測定 |
| SPI mode / clock | ⬜ | キャプチャ。`SPISettings` の周波数が上限宣言どおりかを初めて見られる |
| `shiftOut` / `pulseIn` の実波形 | 🔧(ハングしないことのみ) | キャプチャ。ソフト実装の bit 幅は `delayMicroseconds` 依存なので実測が要る |
| 割込 latency(Q-021 の残り) | ⬜ | 刺激 GPIO の立ち上がり → target の応答 GPIO を**同一時間軸**で測る |
| timing tolerance の決め方(Q-052) | ⬜ | 上記が測れて初めて決まる |
| USB-PD(X035) | ⬜「専用治具」 | 別枠(§8-5 で board 側の電圧と併せて確認) |

### 2.2 相手役がいないので止まっているもの

[test-strategy](test-strategy.ja.md) にこう書いてある — **「passive な logic analyzer だけでは入力側 API を
検証できません。fixture controller または専用 peer から、GPIO edge、既知電圧、UART/SPI/I2C transaction、
NACK、bus stuck 等の刺激を与えます」**。その peer が今は存在しない。

| コアの項目 | 現状 | harness の当たり方 |
|---|---|---|
| `Wire` slave のデータ経路 | 🔧 **「配線待ち」**(自己検査14項目は 4 board pass 済み) | I2C master を演じる。ジャンパ2本+pull-up の治具が不要になる |
| I2C を**実デバイス相手**で | ⬜「これから」 | 模型を演じる(§6) |
| SPI を実デバイス相手 / loopback | ⬜ | SPI slave(PL022 slave)を演じる |
| I2C の NACK / bus recovery / clock stretch | ⬜ | **障害注入**。指定 byte で NACK、SCL 保持でバスハング |
| Serial のフロー制御・エラーフラグ | ⬜ | UART peer から framing error / break を注入 |
| `digitalRead` / EXTI の**他ポート実エッジ** | 🔧 (`gpio_loopback` 実機未実行) | 刺激 GPIO。ジャンパ不要 |
| `analogRead` の確度(既知電圧) | ⬜(範囲のみ) | PWM+RC 疑似 DAC、または外付け DAC |
| SoftSPI / SoftWire / SoftSerial の検証 | 案のみ | 現案は[software-peripherals](software-peripherals.ja.md) の「ソフト実装とハード実装を同じ2 pad に配線して相互通信」。**相手が harness なら、ハード実装が正しい前提を置かずにソフト実装を検証できる** |
| 低消費電力モード | ⬜ **未実装** | シャント + INA で電流測定 |

### 2.3 LinkE 由来で運用が痛いもの(全部実測済み)

| 症状(出典: [upload-and-fixture](upload-and-fixture.ja.md) / [TEST_PLAN](../tests/TEST_PLAN.ja.md)) | harness だとどうなるか |
|---|---|
| **attach のたびに target の `RCC_CFGR0` と `FLASH ACTLR` が書き換わる**(V307: PLL×12→×15)。probe-rs でも ch32rv でも同じ = **probe firmware の挙動**。RCC だけは probe 経由で検証できず、sketch 側で読んで `SystemInit()` で戻している | **書かない実装ができる**。しかも自己観測で証明できる(§5-1) |
| WCH OpenOCD は attach で **flash 先頭(addr 4 から 48 byte の nop + `0x34` に `ebreak`)を書き換え、戻さない** → `load` が省略できない([debugger](debugger.ja.md)) | 同上。debug 用に flash を汚さない contract を明示できる |
| UART bridge: **送るものがある時しか吐かない**(行の途中で止まる)/ **flash 後に配送が止まる**(port 開き直しで復旧)/ **PING→PONG に約5秒**、handshake 12秒、1ステップ最低10秒待ち | native USB CDC なら往復 ~1 ms。**HIL 1 ケースの支配的コストが消える** |
| **16.7 KB の書込で probe が無応答化**。USB 再接続でしか戻らない(`USBDEVFS_RESET` 不可) | watchdog + host からの自己 reset を仕様に入れられる(§5-7) |
| firmware **2.11 では書けても走らない、2.12 で正常**。OSS に更新経路が無く、更新は Windows 専用ツール | firmware が repo にあり、UF2 で全 OS から更新できる。**fixture manifest の `probe_firmware` が自分の管理下に入る** |
| LinkE の壊れ読み値(RedetectChip で復旧)、CH549 の stale fast-read(偽 verify-mismatch) | 自作なら再現条件を自分で潰せる |
| WSL の `vhci_hcd` が **8 port**。ベンチは probe 6 台で既に窮屈 | **1 台 = 1 USB device に畳めれば LA を足しても port が増えない**(§5-6) |

### 2.4 デバッグ出力の 3 経路が LinkE / 特定ツールに縛られている

| 経路 | いまの host 側 | 制約 |
|---|---|---|
| `SerialSDI` | `wlink sdi-print enable` → probe の CDC | **LinkE 専用**。初代 Link 不可 |
| `SerialRTT` | `probe-rs attach` + **ELF が要る** | Serial Monitor で読めない |
| `SerialDMDATA` | `minichlink -T` | 同上。pluggable monitor が無い |

3 つとも**中身は DMI 越しのメモリ/レジスタ操作**なので、**dmibridge を喋る probe なら 3 つとも native に
扱える**。CDC ポートとして出せば `SerialSDI` が LinkE 専用でなくなり、RTT/DMDATA の pluggable monitor
未実装([debug-output](debug-output.ja.md) の課題)も同時に片付く。
`generic-probe-design.ja.md` §8 では優先度 8 だが、
**コア側は既に 3 本のライブラリを出荷予定なので、優先度を上げてほしい側**にいる。

---

## 3. 有効性の評価

### 3.1 効くところ(強い順)

1. **方法4 の解禁**。⬜ が 6 項目まとめて動き出す。しかも Q-050(「後から変更するコストが最も高い項目」)が
   **部材購入と治具製作の話から、firmware とピン割当の話になる**。やり直しが効く。
2. **入力側 API の検証**。今は「DUT が自分で自分を検査する」か「ジャンパで自分に繋ぐ」しか無い。
   独立した相手役ができると、**コアの実装が正しい前提を置かずに検証できる**。
3. **時間軸が 1 本になる**。コアの HIL protocol(`READY?` → arm → `RUN` → `EVENT` → `DONE`)は今、
   host の時計・LA の時計・DUT の時計を人間が繋いでいる。harness ならこれが 1 本になり、
   [test-strategy](test-strategy.ja.md) が要求している**「縮退時にも測定開始点を一意に決められる」**が
   構造的に満たせる。
4. **target を汚さない**(§2.3 の 1〜2 行目)。
5. **ベンチの無人化**。[TEST_PLAN](../tests/TEST_PLAN.ja.md) の「案 b: 別ホストを runner にして board farm」
   の前提条件が「人が USB を挿し直さないと復旧しない probe」で潰れている。ここが外れる。

### 3.2 効かないところ・誤解しないほうがいいところ

| 論点 | 実際のところ |
|---|---|
| **連続ストリーミングは class 次第** | RP2040 は USB FS(実効 ~1 MB/s)で、無圧縮なら 16ch ~500 kSa/s / 8ch ~1 MSa/s が上限。FX2LP は USB HS で 16ch@12 MHz を流し続けられる。**ただし ESP32-S3 の PSRAM(MB 級バッファ)や ESP32-P4 の USB HS を選べば、この不利は消える**([harness-requirements](harness-requirements.ja.md) §2.4) |
| **ただしコアの方法4 は全部「短い窓」** | UART 1 フレーム 87 µs、I2C 10 byte @100 kHz で ~1 ms、PWM 数周期。**burst(trigger + RAM 深掘り)で全部足りる**。深さ(16ch@10 MSa/s ≈ 10 ms)は概算だが桁は合う |
| 全 24 series を書けるようになるわけではない | 線層は SWIO(V003/V00x/M030)と RVSWD(それ以外)の 2 つで、RVSWD は `attested` 止まり。**書込 probe としての完成は遠い**。だから §0-2 の順序 |
| GDB / `arduino-cli debug` はそのままでは繋がらない | いまの経路は WCH OpenOCD 固定([debugger](debugger.ja.md))。harness を使うなら ch32rv 側の GDB server 経由になる。**ch32rv の gdb server は attach を保持するセッション型なので、harness セッションと同じ DUT を取り合う**(ch32rv `0006` §2.3) |
| 「LinkE を捨てる」話ではない | 当面は **LinkE が焼き、harness が観る**。LinkE を認定 probe として残したまま harness を足すのが、リリース経路に対して最も安全 |

### 3.3 コアが依存してよい範囲(提案)

| 範囲 | 提案 |
|---|---|
| ベンチ / 手動テスト(`tests/manual/`) | **採用してよい**。既に人が介在する層なので、失敗してもリリースに波及しない |
| CI(self-hosted runner、board farm) | **段階的に**。fixture health の self-test が揃ってから |
| 同梱アップローダ | **1.0 前は不可**。[ch32rv-requests](ch32rv-requests.ja.md) の「同梱は ch32rv に一本化」を動かさない |
| 対外的な位置づけ | [R-17](research/upload-programmers.ja.md) の互換書込器 Tier に **Tier 3(実験的)** として載せ、実績で Tier 2 へ上げる |

---

## 4. コア側から見た、順序についての意見

`probe-pattern-coexistence.ja.md` §5 の順序と、コア側の困り具合で並べた順序を並べる。
**probe 側の順序は「リスクの高いものを早く潰す」で、コア側は「詰まっているものを早く外す」なので、
違って当然**。突き合わせたい。

| probe 側 §5 | コア側の価値順 | 理由 |
|---|---|---|
| 1. W(書込のみ) | **最後でよい** | ch32rv + LinkE で回っている。困っていない |
| 2. L(キャプチャ + SUMP/sigrok) | **1 番** | 方法4 が 6 項目まとめて動く。target 不要で今日始められる |
| 3. WL | **2 番** | §5-1 の「attach で何が線に出るか」の実測。**コア側は `reg_probe` で内側から同じ現象を測っているので、突き合わせられる**(§7-2) |
| 4. PIO I2C slave spike | **3 番** | `Wire` slave の「配線待ち」と「実デバイス相手」が同時に外れる。コア側でいちばん長く止まっている項目 |
| 5. WLE | 4 番 | 障害注入と EmbedBench 接続(§6) |
| 6. WM(複数書込) | コア側の需要は薄い | ベンチは 1 台運用で選択問題を回避している |

**追加提案**: `L` と `WL` の間に「**LinkE の線を覗くだけ**」の段があると、コア側は即日価値が出る。
probe 実装が一切要らず、キャプチャとデコーダだけでよい。§7-2 に実験案。

### 4.1 前提のずれ(ch32rv `0006` §1 の指摘)と、その解き方

ch32rv から**こちらの 2 文書の間に前提のずれがある**と指摘された。**指摘は正しい。**

| 文書 | 言っていること |
|---|---|
| 本文書 §0-2 / §4 | LinkE が焼き、harness は観測と刺激だけ。**`W`(書込)は最後でよい** |
| [harness-testing](harness-testing.ja.md) §4.2 / §5 | **agent の制御チャネルを DMI へ移す**。`dut_agent()` は DMI 経由 |

後者は **harness が DMI を持っていること**が前提なので、`L` 段階では成立しない。
同じずれが §3.1-3「時間軸が 1 本になる」にも出る。
**LinkE が焼き harness が観る構成では、DMI 側は LinkE の時計・capture は harness の時計**で、
時計は 2 本のまま。

**こちらの解き方(提案)**: 対立軸は「観測か書込か」ではなく、**DMI と flash を分ける**ところにある。

| 段 | 要る能力 | コアが得るもの |
|---|---|---|
| `L` | capture のみ | 方法4 が 6 項目動く。時計は 2 本のまま |
| **`M` 相当**(lane 0 本 + **DMI read/write**) | **DMI。flash アルゴリズムは要らない** | **制御チャネルが DMI に載る / 時間軸が 1 本になる / SDI・RTT・DMDATA が probe 非依存になる** |
| `WLE` | + エミュ | 相手役 |
| `W` | + flash アルゴリズム + chip DB | 書込。**コアはここに困っていない** |

`probe-pattern-coexistence` の語彙で言えば、**コアが早く欲しいのは `L` + `M` で、`W` は最後のまま**。
`M` は「lane を attach せずに monitor だけ」なので、**flash を実装せずに DMI だけを持つ段**として読める。
ここが読めるかどうかが、この対立の実体。**裁定は protocol 側**(§8-9)。

---

## 5. 依頼事項(コア → probe)

構想側は既に厚いので、**コア側の既存ルールから出てくる要求で、まだどの文書にも無いもの**に絞る。

### 5-1. attach で target に何を書くかを contract にする(最優先)

- **要求**: 「DMI read しか出さない passive attach」を持ち、`caps` で申告する。
  probe が target に書く可能性のあるものを**全部文書化**する。
- **根拠**: LinkE は attach で `RCC_CFGR0` と `FLASH ACTLR` を書き換える(V307 実測、probe-rs / ch32rv 双方同じ)。
  WCH OpenOCD は flash 先頭 48 byte を書き換えて戻さない。
  この 2 つのせいで、コアは**クロックツリーを probe 経由で検証できない**。
- **harness だけができること**: debug 線の自己観測で「線に出たのはこれで全部」を**証拠として出せる**。
  「書いていないつもり」ではなく「書いていない」が言える。

### 5-2. 論理信号名を probe 側の一級市民にする

- **要求**: チャネル番号ではなく **`MARKER` / `GPIO_OUT` / `INT_IN` / `PWM` / `UART_TX` / `UART_RX` /
  `I2C_SCL` / `I2C_SDA` / `SPI_SCK` / `SPI_MOSI` / `SPI_MISO` / `SPI_CS` / `MCO`** のような
  **名前 → チャネルの写像を host が渡し、キャプチャ出力にも名前が載る**形にする。
- **根拠**: [test-strategy](test-strategy.ja.md) が**「物理 channel 名を test へ書かない」ことを既に義務化**
  している。名前が probe 側に無いと、コアのテストが GP 番号を直書きすることになり、この規則を破る。
- 12 本(+`MCO` で 13)は [upload-and-fixture](upload-and-fixture.ja.md) が列挙済み。
  debug 2 線を足しても **16ch 窓にちょうど収まる**(空き 1 本)。

### 5-3. 測定開始点(origin)の優先順位を仕様に持つ

- **要求**: 測定原点の source を優先順に決め、**どれを使ったかをキャプチャに記録する**。
  案: `MARKER` エッジ > DMI resume の時刻 > 最初のバス事象 > host コマンド受信時刻。
- **根拠**: [test-strategy](test-strategy.ja.md) は**「marker 専用 pin を確保できない小容量 device でも、
  測定開始点を一意に決められることを test する」**と要求している。
  **CH32V003 SOP8 は GPIO が 6 本**しかなく、marker に 1 本割く余裕が無い場面が実在する。
- **harness だけができること**: DMI 側とエミュ側の**両方を自分で持っている**ので、marker pin が無くても
  原点を作れる。LinkE + 外部 LA では作れない。

### 5-4. 成果物(キャプチャ)の形式と provenance

- **要求**:
  1. ライブの protocol とは別に、**sigrok が読める形へ無損失で落とせる**こと。
  2. キャプチャに **channel map / sample rate / 時間軸の source と確度 / probe firmware 版 /
     落としたサンプル数** を同梱すること。
  3. **落ちたサンプルがあるキャプチャは golden へ昇格できない**、という運用を成立させられること。
- **根拠**: [test-strategy](test-strategy.ja.md) は
  「raw `.sr` を保存し、decoder 変更後も CI で replay する」「sigrok/libsigrok/decoder version を固定」
  「golden へ昇格する capture はレビューし content-addressed に保存」を**既に設計している**。
  形式が分岐すると、この replay 層をもう一度作り直すことになる。
  `probe-pattern-coexistence.ja.md` §0-6 の SUMP/sigrok 互換は**ライブ表示**の話なので、
  **ファイルの話を別に決めてほしい**。

### 5-5. 時間軸の確度を `caps` で申告する

- **要求**: 時計の source(水晶/内蔵 RC)と確度、および
  **sample index ↔ µs timer の対応誤差**(`dut-harness-design.ja.md` §4.3 の未決)を申告する。
- **根拠**: コアは判定を **functional signature(byte 列・順序)と timing result(周波数・duty・幅)の 2 層**
  に分ける。timing 側の tolerance(Q-052)は「DUT の誤差」と「測定器の誤差」を分けないと決められない。
  CH32 の HSI は ±1% 級、RP2040 board の水晶は ±30 ppm 級 → **2 桁以上の差があれば HSI トリムを測れる**が、
  それを**宣言してもらわないと主張にできない**。

### 5-6. 識別・排他・USB device 数

| # | 要求 | 根拠 |
|---|---|---|
| a | **個体ごとに一意で安定した USB serial**(RP2040 なら flash unique ID)を descriptor に出す | コアの識別優先順位は「実測で unique と確認した hardware serial」が第 1。`--device 0` や「最初に見つかった 1 台」は**使わないと決めている**。候補が複数残ったら**書込中止**(fail-closed) |
| b | **probe 種別・firmware 版・capability を machine-readable に**出す | fixture manifest が `probe_model` / `probe_firmware` / `probe_mode` を要求。LinkE は `wlink status` に聞くしか無く、これが 2.11/2.12 問題を見えなくしていた |
| c | **per-device advisory lock**(USB serial 単位、OS runtime dir、timeout、stale 回収、専用 exit code) | [ch32rv-requests](ch32rv-requests.ja.md) の依頼 A-2 と**同じ仕様**。harness は control / capture / DUT-UART が同時に動くので、**day 1 から要る**。ch32rv の実装をそのまま踏襲してほしい |
| d | **1 台 = USB device 1 個**(composite で control + capture + DUT UART) | WSL `vhci_hcd` が 8 port。ベンチは probe 6 台で既に窮屈で、**LA を別 device で足すと即詰まる** |
| e | exit code と JSON envelope を **ch32rv の contract に合わせる** | コアのベンチ(`smoke.py` 等)が exit code と JSON に依存している。2 つ目の方言を作らない |

### 5-7. 人手なしで復旧できること

- **要求**: (1) watchdog、(2) host から **USB を挿し直さずに** 完全 reset できる経路、
  (3) **DUT に依存しない self-test**(fixture health preflight 用)。
- **根拠**: LinkE は 16.7 KB の書込で固まり、**USB 再接続でしか戻らない**。
  [upload-and-fixture](upload-and-fixture.ja.md) の preflight は
  「programmer / analyzer / stimulus controller を DUT に依存しない範囲で self-test する」と決めており、
  **fixture health の失敗と core の回帰を別の結果にする**と書いてある。
  board farm([TEST_PLAN](../tests/TEST_PLAN.ja.md) 案 b、「本命」)はここが無いと成立しない。

### 5-8. ピン衝突表を `ch32-device-data` から生成する

- **要求**: `dut-harness-design.ja.md` §8 の series 別衝突表を、
  EVT の `@Note` コメント grep ではなく **`ch32-device-data` の生成物から作る**。
- **根拠**: 既にあるデータのほうが強い。
  - `evidence/debug_wiring.csv` — 27 series の SWDIO/SWCLK pad + 1/2 線両対応の別、
    出典は **WCH-Link User Manual(confidence: confirmed)**。EVT サンプルの推測ではない。
  - `evidence/remap_routes.csv`(4837 行)/ `evidence/pin_alternate.csv` — remap を含む全 route。
  - `dut-harness-design.ja.md` §8 自身が「EVT サンプルが選んだピンであって唯一の組とは限らない」
    「実装時に再確認」と注記している。**その再確認先が既に生成物として存在する**。
- コア側は `boards.txt` / variant / `reg_probe` の期待値を全部この DB から生成しており
  ([device-data](device-data.ja.md))、**3 つ目の手書きピン表が増えるのを避けたい**。

### 5-9. debug 出力 3 経路(SDI / RTT / DMDATA)を native に扱う

§2.4 のとおり。中身は全部 DMI 越しの操作なので、dmibridge probe なら実装できる。
**`SerialSDI` が LinkE 専用でなくなる**こと、**RTT/DMDATA を CDC として出せば
Arduino の Serial Monitor で読める**ことの 2 点が、コアにとっての価値。

---

## 6. EmbedBench との接続(いちばん伸びしろのある提案)

### 6-1. いまの設計だと、演じられるものに制限がかかる

`dut-harness-design.ja.md` §6 は知識境界を
**「probe は器だけ持ち、中身は host が流し込む」**と置いた。dmibridge の
「probe は chip を知らない」の自然な延長で、原則としては正しい。

ただし同節は制限も書いている — **「host backed の可否は『待たせる手段があるか』で決まる」**。
I2C は clock stretch、SD は busy token で待てるが、**UART は待たせる手段が無いので事前ロードが要る**。
往復 1〜2 ms が挟まる以上、これは避けられない。

### 6-2. EmbedBench の模型は「中身」ではなく、そのまま動くコード

| 要素 | 実態 |
|---|---|
| デバイス IF | `src/embedbench_device.h`、**凍結済み(v1 / rev 004)**。include は `<stddef.h>` / `<stdint.h>` のみ。Arduino 型なし |
| 模型 | **23 種**、純粋 C++11、模型のソース 2,996 行。動的確保なし。register-map センサ / SD カード / Modbus slave / UWB / **わざと壊れる部品** まで(数値は EmbedBench `docs/FACTS.ja.md` が正本。**自分で数えない**) |
| 環境実装 | **既に 2 つ**(host 環境 1,457 行 / 純ネイティブ `nenv` 464 行)。「環境はプラットホーム別の実装例」と明記。`tests/conformance/` が**環境の受け入れ試験**を持つ |
| 契約 | 再入禁止・効果の遅延配送・借用バッファ・`advanceTo` 単調・容量超過は必ず診断 |

つまり **harness を 3 つ目の環境にすれば、22 種の模型がそのまま本物の線の上で動く**。
host に問い合わせる必要が無いので、**§6-1 の「待たせる手段」制限が消える**(UART も演じられる)。
firmware に載る量は模型 2,996 行で、RP2040 には過剰なほど余裕がある。

### 6-3. 得られるもの: 同じ模型を 2 つの環境で走らせて diff する

```text
同じテストケース ─┬─ EmbedBench(host、仮想時計)  → 1行1イベントのログ
                  └─ harness(実機、本物の線)      → 同じ形式のログ
                                                       ↓
                                          行単位 diff = 実機だけで起きること
```

- **host 側が保証するもの**: アプリのロジック、順序、境界値、エラー経路。実機不要・毎 PR で回せる。
- **実機側だけが出すもの**: タイミング、電気、コアの HAL 実装、クロック、割込 latency。
- 差分に残るものが**コアのバグ候補になりうる**。
  「host では通るのに実機で落ちる」を**波形ではなくイベント列で**説明できる。

> **ただし差分はバグだけではない**(EmbedBench `HARNESS_REQUESTS` §6 の指摘)。
> 少なくとも次が乗る — **模型が意図的に圧縮している時間定数**(6 種)、**仮想時計と実時間の時刻**、
> **記録の畳み込み**(容量の違う 2 環境は畳む位置が違い、行数が合わない。実測で 27,375 イベントが 61 行)、
> 効果の遅延配送が実時間を持つこと。
> **素の行単位 diff は時刻を含むので全行が差分になる。**
> 答えはコアが既に持っている 2 層判定で、**`(origin, text)` の列は完全一致・時刻は許容差**で比べる。
> 段取りは「**最小の模型 1 つでノイズをゼロにしてから増やす**」。

[test-strategy](test-strategy.ja.md) は既に
**「同じ portable sketch を CH32 profile と host profile で compile して比較する」**
**「`#ifdef HOST` / `#ifdef CH32` で期待動作そのものを分岐させない」**
と決めているので、**この形はコアの既存方針の素直な延長**になる。

### 6-4. そのために決めないと進まないこと

| # | 論点 | 案 |
|---|---|---|
| 1 | **時間の意味が変わる** | host は `advanceTo` で時計を進める。実機では時計が勝手に進む。→ 実時間 µs timer から `advanceTo` を駆動する。契約(単調非減少・同値再呼び出し可・飛びは期限順に処理)は守れる |
| 2 | **遅延配送が実時間を持つ** | 「Device メソッドから戻ってから配送」は、実機では**数 µs 後**になる。master が先に進んでいる可能性がある。→ **ずれたら必ず診断を出す**。無音で吸収しない(EmbedBench の既存 idiom と同じ) |
| 3 | **模型が間に合わない場合** | 実機では master が待ってくれない。→ **それ自体が測定結果**。「この模型はこの clock では応答が間に合わない」は有用な事実。隠さず記録する |
| 4 | **ログ形式** | 行単位 diff が成立するよう、**同一形式**にする。ここが分岐すると提案全体の価値が消える |
| 5 | **どちらの repo に置くか** | **回答あり(EmbedBench `HARNESS_REQUESTS` §5): probe 側を推す。** 模型は独立した Arduino ライブラリで、harness が `embedbench_device.h` + `devices/` に依存するのが**想定した消費形**。EmbedBench 側に置くと RP2040 のツールチェーンが入り CI も覆えない |
| 6 | **EmbedBench の方針との整合** | **回答あり: probe 側に置くなら触れない。** 「別環境への移植」は EmbedBench 自身の移植をしない宣言であって、他所が凍結 IF に対して環境を書くのは **IF が設計どおり働いている状態** |
| **7** | **圧縮された時間定数(E-1、最優先)** | **模型 6 種が時間定数を意図的に圧縮している。** 最悪は Modbus で、実物 4,010 µs に対し模型 1,500 µs。9600 baud の 1 文字(1,146 µs)よりわずかに長いだけなので、**DUT の送信途中でフレーム完結と判断しうる**。§9 で扱ったのは「遅すぎて成立しない」だが、こちらは**速すぎて誤動作する**方向。(a) 実物値へ設定できる経路を用意するか (b) 圧縮模型を peer に使わないか、を選ぶ。**EmbedBench 側が (a) の実装を引き受けられる** |
| **8** | **`requestWake`(E-2)** | 基底実装は何もせず `false` を返すので、**応えられない環境では模型が静かに劣化する**(EmbedBench で実際に踏んだ)。`caps` に「時刻要求に応えられるか」と分解能を載せ、要る模型を束縛したら fail closed。**呼ぶ模型は 12 種** |
| **9** | **再入禁止(E-3)** | 模型 23 種すべての前提。実機の割り込みで破られると状態機械が壊れる。**保留の容量が溢れたら必ず診断**(無音で捨てない) |
| **10** | **診断語彙とイベント行(E-4)** | 準拠キットは語彙を検査していないので、**語彙が違っても両方とも準拠**。行単位 diff をやるなら揃える。やらないなら「diff は機能的な署名だけ」と最初に決める |
| **11** | **`caps` に凍結 IF の版(E-5)** | `embedbench_if: { version, revision }`。模型の版とは別に効く |

### 6-5. 逆向きの価値(EmbedBench 側から見て)

EmbedBench の `DEVICE_IF_SCOPE.ja.md` §3.3 は
「線を越えた要望は**実機テスト・専用シミュレータ・純関数の単体テスト**へ振り分ける」と書いている。
**その「実機テスト」の受け皿が実在すると、この線引きが強くなる**。
いまは「範囲外」と書いた先が空白なので、要望が来るたびに個別に断ることになる。

---

## 7. 提案する次の一手(小さく、証拠が出るもの)

### 7-1. 順序の突き合わせ

§4 の表を probe 側の作業順と突き合わせる。特に **`W` を後ろに回してよいか**。
コア側は書込に困っていないので、回してよければ **`L` に全部の初速を回せる**。

### 7-2. 最初の共同実験: 「LinkE が attach で線に何を出しているか」

**probe の実装が一切要らない**(キャプチャ + デコーダのみ)。

| | 内容 |
|---|---|
| 測るもの | LinkE ↔ target の RVSWD 線を harness でキャプチャし、DMI トランザクション列へデコード |
| 材料(コア側) | ベンチに V307 / V203 / V103 / L103 / X035 / V003 の 6 枚。`probe_switch` で 5 秒で切替。**`reg_probe` が同じ現象を内側から測っている**(`RCC_CFGR0` の変化と `SystemInit()` による heal 回数) |
| 出るもの | (a) `link-to-target.ja.md` §3 の RVSWD bit 仕様が `attested` → `verified`、(b) **RCC を書き換えているのがどの DMI 書き込みか**が確定 → コアは初めて「何が起きているか」を書ける、(c) ch32rv の capture 計画(USB 側のみ)に**線側の半分**が付く |
| コスト | キャプチャ 16ch のうち 2ch。デコーダは DMI フレーム(addr 7 + data 32 + op 2 + parity 1)×2 相のみ |

**3 つの repository が同時に得をする実験**で、しかも harness の最小構成(`L`)で足りる。

### 7-3. コア側で先にやっておくこと(probe への依頼ではない)

- 論理信号名 12(+`MCO`)を**確定させて文書に固定**する
  ([test-strategy](test-strategy.ja.md) が既に義務化しているので追認)。
  → §5-2 の写像を probe に渡せる形にする。
- **Q-050(LA channel / connector / 電源)の判断を harness の結論が出るまで保留**にする。
  16ch LA + FX2LP + 治具の**購入判断を先にしない**。
- [R-17](research/upload-programmers.ja.md) の Tier 表に harness を **Tier 3** で載せる枠を用意する。
- `tests/manual/` に harness を使う case を置く場所を決める(既存の 1 case 1 ディレクトリ規約に従う)。

---

## 8. 未決・確認したいこと

1. **§4 の順序**。`W` を後ろに回してよいか。probe 側のリスク順を優先するなら、コア側は待つ。
2. **§6 の EmbedBench 接続を追うか**。追うなら EmbedBench の「当面やらないこと」に触れるので、所有者判断が要る。
3. **§6-4 の 5**: harness 用の環境実装をどの repository に置くか。
4. **board 選定**。`harness-board-survey.ja.md` は
   「治具に量産するなら RP2040-Zero、開発機は Pico(無印、23ch 窓)」としている。
   コア側の 13 論理信号 + debug 2 線 = 15 なので **Zero の 16ch 窓に収まる**が、
   開発中は Pico のほうが楽そうに見える。ここは probe 側の判断に従う。
5. **5V target**。V003 は 3.3/5.0V 動作。ベンチの board が 5V で動いているものがあるかは未確認。
   RP2040 は 5V トレラントではない(`harness-board-survey.ja.md` の軸 G)。
6. **本文書を open-questions へ起票するか**。Q-045(ESP32系/RP2040 programmer を開発するか、P2)は
   「書込器を作るか」の問いで、本文書の主眼(計測 fixture)とは別の論点になっている。分けるか統合するか。

---

## 9. 参照

**コア側**: [test-strategy](test-strategy.ja.md) / [upload-and-fixture](upload-and-fixture.ja.md) /
[../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) /
[open-questions](open-questions.ja.md)(Q-021, Q-044, Q-045, Q-050, Q-052) /
[ch32rv-requests](ch32rv-requests.ja.md) / [debug-output](debug-output.ja.md) / [debugger](debugger.ja.md) /
[software-peripherals](software-peripherals.ja.md) / [device-data](device-data.ja.md) /
[R-17](research/upload-programmers.ja.md) / [../tests/manual/reg_probe/](../tests/manual/reg_probe/)

**外部 repository**(パスは開発環境のもの):

- wch-protocols: `references/dut-harness-design.ja.md` / `references/harness-board-survey.ja.md` /
  `references/probe-pattern-coexistence.ja.md` / `references/generic-probe-design.ja.md` /
  `protocols/link-to-target.ja.md` / `protocols/dmi-bridge.ja.md`
- ch32rv: **`docs/data-requests/0006-harness-integration.ja.md`(要望と実測値)** / `docs/architecture.ja.md` / `docs/protocol/wch-link.ja.md`
- EmbedBench: **`docs/HARNESS_REQUESTS.ja.md`(要望と回答)** / `docs/FACTS.ja.md`(**引用してよい数値の正本**)
- 索引: wch-protocols `references/harness-index.ja.md`
- EmbedBench: `docs/DEVICE_IF_SCOPE.ja.md` / `docs/DEVELOPMENT_PLAN.ja.md` /
  `docs/RELEASE_SHAPE.ja.md` / `src/embedbench_device.h`
- ch32-device-data: `evidence/debug_wiring.csv` / `evidence/remap_routes.csv` / `evidence/pin_alternate.csv`
