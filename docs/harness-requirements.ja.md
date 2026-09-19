# harness への要求カタログ(コア側)

文書状態: **要求の列挙**。**決定でも合意でもない**
文書基準日: 2026-09-06

## この文書の位置づけ

**最終的な調整は protocol 側(`wch-protocols`)で、各リポジトリの要望を突き合わせて行われる。**
いまは**要求を絞る段階ではなく広げる段階**なので、この文書は
**ArduinoCore-CH32 から見て「あると嬉しい」を、実現性で足切りせずに並べる**ことを目的にする。

したがって:

- **相反する要求も両方載せる。** どちらを採るかは protocol 側の裁定に委ねる(§3)。
- **コアとして「どちらでもよい」ものは、そう明記する**(§4)。裁量を返すのも情報。
- **まだ要求の形になっていない気づきも枠として残す**(§5)。
- ここに書いた優先度は**コア内での重み**であって、harness 開発の順序ではない。

先行する 3 文書の内容もここへ ID 付きで統合した。詳細な根拠はそちらにある。

| 文書 | 中身 |
|---|---|
| [development-probe-functional-spec](development-probe-functional-spec.ja.md) | **機能から読む入口**。ペリフェラルごとの必要機能、検証範囲、限界、能力classと横展開level |
| [harness-probe](harness-probe.ja.md) | 採る/採らないの評価、コアの穴との対応、**§4.1 = DMI 段階のずれの解き方**、EmbedBench 接続 |
| [harness-wiring](harness-wiring.ja.md) | series 別の pad、16ch class の割当 5 案、必要な相手の数、class 別の到達範囲 |
| [harness-testing](harness-testing.ja.md) | 誰がどう叩くか、能力宣言と resolver、模型の置き場所 |

**この 3 文書の「結論」はコア側の立場であって、protocol 側の決定ではない。**

### 他リポジトリの要望を読んだ結果(2026-09-06)

所在の索引は `wch-protocols/references/harness-index.ja.md`。

| repo | 文書 | ID 体系 | ここへの反映 |
|---|---|---|---|
| **ch32rv**(ライタ) | `docs/data-requests/0006-harness-integration.ja.md` | 節番号 | §1-O に H-180〜H-184、H-108 / H-109 を新設。H-002 / H-126 の根拠を訂正。C-12 / C-13 を追加 |
| **EmbedBench**(ベンチ) | `docs/HARNESS_REQUESTS.ja.md` | **`B-001`〜`B-007`** | §1-O に H-185〜H-191 |
| **wch-protocols** | `references/harness-index.ja.md` ほか | 節番号 | 裁定はまだ無い |

**どの指摘をどう反映したかは §6 に 1 枚でまとめた**(相手側が解消済みに線を引けるように)。

### `H-nnn` / `C-n` が共有 ID 空間になったことへの対応

索引 §4 が「**`H-nnn` / `C-n` が事実上の共有 ID 空間になった。正本はコアの `harness-requirements`**」と
整理した。ライタもベンチも独自採番せずにこの ID を引いて回答・訂正・参照実装を返している
(索引 §3.1 の型 ②③④)。**採番の正本を明示的に決めるかは索引 §6-2 のまま未決**だが、
現状に合わせて**こちらは次を守る**。

- **ID を再利用しない。** 取り下げた要求も番号は空けたまま残し、取り消し線で扱う。
- **ID を振り直さない。** カテゴリの並べ替えが必要になっても番号は動かさない。
- **範囲を文書内に書かない。** 総数は書いた時点で腐るので、**数えるのは各表**。
  (索引も同じ理由で「引用するときは各文書を見る」としている)
- **他 repo の ID を引くときは出どころを併記する**(`B-001` はベンチ、`0006 §14-1` はライタ)。

## 読み方

| 列 | 意味 |
|---|---|
| **重み** | `◎` コアの未着手項目を直接外す / `○` 効果は大きいが代替がある / `△` あると嬉しい / `?` 要求になるか未確定 |
| **状態** | `実測` コア側で数値を取ってある / `文書` コアの文書に規則として書いてある / `推定` 根拠は状況証拠 / `願望` |

---

## 1. 要求一覧

### A. 識別・排他・運用

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-001 | 個体ごとに一意で安定した USB serial を descriptor に出す | ◎ | 文書 | コアの識別優先順位は「実測で unique と確認した hardware serial」が第 1。`--device 0` や「最初に見つかった 1 台」は使わないと決めている |
| H-002 | probe 種別 / firmware 版 / capability を machine-readable に出す | ◎ | 実測 | fixture manifest が要求。**根拠を更新(ch32rv `0006` §12)**: ch32rv は `probe info --json` が種別と版(raw / 正規化 / WCH 表記の三重)を、`capabilities --json` が probe×target の可否と**理由**を出す。**要求そのものは有効**で、harness も同じことをすべき |
| H-003 | per-device advisory lock(USB serial 単位、runtime dir、timeout、stale 回収、専用 exit code) | ◎ | 文書 | ch32rv 依頼 A-2 と同一仕様。harness は control / capture / UART が同時に動くので day 1 から要る |
| H-004 | 1 台 = USB device 1 個(composite) | ◎ | 実測 | WSL `vhci_hcd` が 8 port。ベンチは probe 6 台で既に窮屈 |
| H-005 | exit code と JSON envelope を ch32rv の contract に合わせる | ○ | 文書 | ベンチのスクリプトが 2 つ目の方言を覚えない |
| H-006 | 候補が複数なら **fail closed**(黙って 1 台を選ばない) | ◎ | 文書 | upload-and-fixture の既定 |
| H-007 | `board-identify` に載る(人が `by-id` で引ける) | ○ | 願望 | todo に「`board-identify` に CH32 probe を追加」がある |
| H-008 | 常時接続できる台数の上限を運用に織り込める(いまは 4 台) | △ | 実測 | todo「常時つなげるのは 4 台」 |
| H-009 | 1 台の harness に複数 DUT(lane / mux)を将来足せる余地 | △ | 文書 | Q-043。ただし 1 target 専有と排他になる(§3 C-2) |

### B. 能力宣言と束縛

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-010 | `caps` の channel は**番号のみ**。論理名(`MARKER` 等)を probe の語彙に入れない | ◎ | 文書 | 論理名は host の辞書。test-strategy が「物理 channel 名を test へ書かない」を義務化済み |
| H-011 | 機能集合・排他グループ・連番制約を申告する | ◎ | 文書 | host はこれだけを見て組み合わせを決める |
| H-012 | 数値上限はすべて `caps` 側。protocol に焼かない | ○ | 文書 | `dmi-bridge` の設計原則 2 と同じ |
| H-013 | `configure` は原子的。成立しなければ丸ごと拒否、部分適用しない | ◎ | 願望 | 半分設定された harness で走ると、失敗の出どころが分からなくなる |
| H-014 | `caps` の申告と実装が食い違ったら `configure` が落ちる | ○ | 願望 | 宣言を信用できる状態に保つ |
| H-015 | `caps` に「器」と「組み込み模型(名前 + 版)」を並べて申告する | ◎ | 願望 | ベンチごとに載っている模型が違いうる |
| H-016 | capture unit の数を `caps` が申告(1 unit 実装と 2 unit 実装が同じ仕様で共存) | ○ | 文書 | dut-harness §5 案 C の扱いと同じ |
| H-017 | 時間軸の source / 確度(ppm)/ sample index ↔ µs の対応誤差 | ◎ | 推定 | timing tolerance(Q-052)は「DUT の誤差」と「測定器の誤差」を分けないと決められない |
| H-018 | attach で target に書くものを `caps` に出す(理想は空) | ◎ | 実測 | LinkE は attach で `RCC_CFGR0` と `FLASH ACTLR` を書き換える |
| H-019 | pull-up の所在・直列抵抗・電圧をベンチ側から宣言できる | ○ | 文書 | dut-harness §3.4。I2C は open-drain なので所在が判定に効く |
| H-020 | **ベンチに載っている本物のデバイス**も同じ語彙で宣言できる | ○ | 願望 | テストからはエミュか実物かを気にせず「I2C に 24C02 がいる」と書きたい |
| H-021 | 束縛が 2 通り以上あるときは fail closed | ○ | 文書 | H-006 と同じ規則を resolver にも |

### C. 制御と transport

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-030 | テスト本体はセッション。**CLI 毎回起動にしない** | ◎ | **実測** | `reg_probe` は 1 レジスタ 1 プロセス起動で、1 board 30〜130 秒の大半がそれ |
| H-031 | transport を抽象化(`serial://` / `usb://` / `socket://`) | ◎ | 願望 | — |
| H-032 | `socket://` を第一級に(遠隔ベンチ / mock) | ◎ | 文書 | TEST_PLAN の board farm 案 b。EmbedBench は既に `socket://localhost` を使っている |
| H-033 | CLI とライブラリが同一 wire protocol を共有 | ○ | 願望 | CLI は診断用、セッションはテスト用で用途が違うが方言は 1 つ |
| H-034 | mock / 参照実装を protocol と一緒に出す | ○ | 願望 | 実機なし CI の入口。protocol を 1 つに保つ |
| H-035 | capture ストリームと control を別チャネルに | ○ | 文書 | `probe-pattern-coexistence` R2 |
| H-036 | Arduino 専用にしない(`ch32fun` 等からも同じ CLI/API) | ◎ | 文書 | upload-and-fixture の既定方針 |
| H-037 | 遠隔時の認証 | ? | 願望 | localhost だけなら不要。board farm で要る |

### D. キャプチャと時間軸

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-040 | sigrok が読める形へ**無損失で落とせる** | ◎ | 文書 | test-strategy が raw `.sr` 保存と CI replay、decoder version 固定を既に設計している |
| H-041 | capture に provenance 同梱(channel map / rate / 時間軸 / fw 版 / 欠落数) | ◎ | 文書 | 同上 |
| H-042 | 落としたサンプル数を申告する。**無音の欠落を作らない** | ◎ | 文書 | コアと EmbedBench の共通 idiom |
| H-043 | 測定原点(origin)の優先順位を決め、どれを使ったか記録する | ◎ | 文書 | test-strategy が「marker 専用 pin を確保できない device でも原点を一意に決められることを test する」と要求。V003 SOP8 は GPIO 6 本 |
| H-044 | burst と continuous の 2 モード | ○ | 推定 | 方法4 の項目は全部短い窓だが、UART の長時間監視は continuous |
| H-045 | pre-trigger バッファ | ○ | 願望 | 「事象の直前」を見たい |
| H-046 | cross-domain trigger(バス事象 → DMI halt、ADC 閾値 → 刺激) | ○ | 文書 | dut-harness §2.2。LinkE + 外部 LA では作れない |
| H-047 | 自己観測(自分が駆動した線を同じ窓で読む) | ○ | 文書 | dut-harness §5 案 A。H-018 の証明手段でもある |
| H-048 | 2 台分業時の時間軸較正(共有 trigger のエッジで) | △ | 文書 | harness-board-survey §5 |
| H-049 | 論理信号名 → channel の写像を host から渡し、出力にも名前が載る | ◎ | 文書 | H-010 の対。名前が probe に無いとテストが GP 番号を直書きすることになる |

### E. 相手役(器と模型)

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-050 | UART peer ×2 | ◎ | 実測 | hw UART0/UART1 で足りる |
| H-051 | I2C target ×2 アドレス | ◎ | 実測 | hw I2C0 + I2C1 を別ピンに出せば PIO 不要 |
| H-052 | I2C target 多アドレス(1 バスに 3 個以上) | ○ | 願望 | PIO I2C target。I2C デバイス走査の試験に効く |
| H-053 | SPI target ×1(mode 0〜3、bit order) | ◎ | 文書 | コアの SPI は controller 専用で、実デバイス相手が未着手 |
| H-054 | 模型は probe 内で完結して応答する。**転送ごとに host へ問い合わせない** | ◎ | **実測** | SPI/UART は待たせる手段が無い。I2C は clock stretch で成立するが実物の 20 倍遅くなり、`Wire` の 25 ms/待ち を圧迫する |
| H-055 | preload / inject / readback を転送とは別経路で受ける | ◎ | 文書 | EmbedBench の `reset`/`channelWrite`/`channelRead`/`dump` は effect-free と契約されていて critical path の外にある |
| H-056 | 模型に版を付け、版違いは fail closed | ○ | 願望 | ベンチ間の差を隠さない |
| H-057 | 模型はビルド時プロファイルで選ぶ(全部載せない) | ○ | 文書 | 「能力は build 時、役割は実行時」 |
| H-058 | 汎用の器(register file / block device / byte stream)で足りるものは firmware 更新なしで足せる | ◎ | 文書 | dut-harness §6 の原則 |
| H-059 | SD カード(SPI mode)の相手 | ○ | 願望 | EmbedBench に `unit_sdcard_model` が既にある |
| H-060 | SPI ディスプレイを演じて framebuffer を host へ | △ | 文書 | dut-harness §2.4。**TinyGFX と共用できる**(§N) |
| H-061 | 1-Wire / WS2812 / IR など、コアが外部ライブラリに委ねたプロトコルの相手役 | △ | 願望 | software-peripherals が「コアの範囲外」とした先。相手役があると外部ライブラリ側が助かる |
| H-062 | ARGB(hw、V407/V467/X305/X315)の受信・デコード | △ | 推定 | 1 線。PIO で読める |
| H-063 | PIOC(X033/X035/V205)の相手役 | △ | 推定 | 2 ピンのプロトコルエンジン。何を喋らせても相手になれる |

### F. 刺激と注入

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-070 | GPIO 刺激(別ポート・別 bit の EXTI) | ◎ | 文書 | EXTI 線は bit 番号で決まる。`gpio_loopback` はジャンパ待ちで実機未実行 |
| H-071 | I2C 注入: 指定 byte で NACK / clock stretch T µs / SCL 保持 / SDA グリッチ | ◎ | 文書 | 入力側 API の検証は passive LA では不可、と test-strategy が明記 |
| H-072 | SPI 注入: busy 応答 / 応答遅延 / MISO 保持 | ○ | 文書 | 同上 |
| H-073 | UART 注入: framing error / break / byte 落とし / パリティ誤り | ◎ | 文書 | コアの「フロー制御・エラーフラグ」が未着手 |
| H-074 | UART フロー制御(RTS/CTS)の相手 | ○ | 文書 | 同上 |
| H-075 | バス事象に同期した NRST | ○ | 文書 | dut-harness §2.3 |
| H-076 | trigger から T µs 後の VDD 切断(brown-out) | ○ | 文書 | 同上 |
| H-077 | 既知パルス幅の生成 | ◎ | 実測 | `pulseIn` の自己検査は自分のパッドを測っていて外部源が無い |
| H-078 | 既知周波数の生成(入力捕捉の検証) | ○ | 推定 | TIM の入力捕捉が未検証 |
| H-079 | 長時間の刺激(`millis()` の wraparound 近傍、RTC) | △ | 文書 | test-strategy は「実時間で待たない」と決めているが、実機での確認手段は別途要る |

### G. アナログ・電源・電流

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-080 | 既知電圧の出力(`analogRead` の確度) | ◎ | 文書 | todo「ADC 分解能は datasheet 由来。実機で確認する(要実機)」 |
| H-081 | DAC 出力の測定 | ○ | 実測 | V303/305/307/317/407/467 の `PA4`/`PA5`。SPI1 の NSS/SCK と同じ pad |
| H-082 | VDD 測定(立ち上がり、brown-out) | ○ | 文書 | dut-harness §7 |
| H-083 | 電流測定(低消費電力、sleep 電流) | ◎ | 文書 | コアの「低消費電力モード」は**完全に未実装**。数値が出せる手段が無い |
| H-084 | 電源制御(on/off、power cycle) | ◎ | 文書 | fixture health、unbrick(power-off erase)、brown-out |
| H-085 | 5V target の扱い(分圧 / レベル変換) | ○ | 推定 | V003 は 3.3/5.0V 動作。RP2040 は 5V トレラントではない |
| H-086 | **VBUS 電圧の測定と安全インターロック** | ◎ | 実測 | todo: PD の 5V 以外 request は「VBUS が board の電源系に入る配線なら危険(5V 専用 LDO に 9〜20V)」。**board を焼かないための要求** |
| H-087 | アナログ刺激の分解能(PWM+RC で足りるか、外付け DAC が要るか) | ○ | 願望 | 実測してから決める |
| H-088 | OPA / コンパレータの相手(アナログ入力を与えて出力を見る) | △ | 推定 | コアでは `対象外` だが CH32 固有機能 |
| H-089 | TouchKey の相手(既知容量の切替) | △ | 推定 | 同上 |
| H-090 | 温度・電圧を変えた条件での再測定 | ? | 願望 | HSI トリムの温度依存を見るなら。将来枠 |

### H. debug 線と target 非破壊

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-100 | **passive attach**(DMI read しか出さない)を持ち、`caps` で申告する | ◎ | **実測** | LinkE は attach で `RCC_CFGR0` と `FLASH ACTLR` を書き換え、コアはクロックツリーを probe 経由で検証できない |
| H-101 | target に書くものを全部文書化する。**監査対象はメモリだけでなく GPR / CSR も含む** | ◎ | 実測 | 同上。**ch32rv `0006` §14-2**: CH32V103 で `AttachChip` が生きた GPR `s1`(x9)を chip id で上書きし復元しない。**メモリは 1 byte も変わらない**ので、メモリだけ見ていると取りこぼす |
| H-102 | flash 先頭を書き換えない | ◎ | 実測 | WCH OpenOCD は addr 4 から 48 byte の nop と `0x34` の `ebreak` を書いて戻さない |
| H-103 | 自己観測で「線に何も出していない」を証拠として出す | ○ | 願望 | H-047 と H-100 の合わせ技。他の probe には作れない |
| H-108 | **attach 前後で全 GPR を読んで diff する**(安価な変種) | ○ | 実測 | ch32rv `0006` §14-2。線を見なくても GPR 破壊は検出できる。**harness が自分でできる** |
| H-109 | **書込後の read がいつ確定するかを仕様に持つ** | ◎ | 実測 | ch32rv `0006` §14-1。CH549 の stale fast-read(stub 直後の bulk read が program 前の像を返す)と、CH32V103 の option 領域(**reset するまで書込前の像を読み返す**)。**harness は「自分が書いたものを自分で読み返す」設計になりやすい**ので、確定規則(reset を挟む / bounded retry / 権威ある経路で再確認)を仕様に持つ |
| H-104 | 1 線 / 2 線を選べることを pad の逃げ道として使える | ○ | 実測 | V005/V006/V007/M007 は 2 線だと SWCLK=`PB3` が SPI_MISO と食い合う。1 線を選べば空く |
| H-105 | attach 直後のレース(CDC が vendor より先に見える窓)を踏まない | ○ | 実測 | 1 秒間隔 3 回の retry で回避している |
| H-106 | 大きい image で固まらない。固まっても USB 挿し直し無しで戻る | ◎ | 実測 | LinkE は 16.7 KB の書込で無応答化し、USB 再接続でしか戻らない |
| H-107 | 壊れ読み値を持ち続けない(RedetectChip 相当が要らない設計) | ○ | 実測 | LinkE の既知 quirk |

### I. デバッグ出力経路(SDI / RTT / DMDATA)

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-110 | SDI print を probe 非依存にする(LinkE 専用をやめる) | ◎ | 実測 | `SerialSDI` は出荷予定のライブラリなのに LinkE でしか読めない |
| H-111 | RTT を native に扱う(ELF なしで扱えるかを含む) | ○ | 実測 | いまは `probe-rs attach` + ELF が要る |
| H-112 | DMDATA を native に扱う | ○ | 実測 | いまは `minichlink -T` |
| H-113 | 3 経路を CDC として出し、Arduino の Serial Monitor で読めるようにする | ◎ | 文書 | debug-output の未実装項目 |
| H-114 | Pluggable Monitor に対応する | ○ | 文書 | ch32rv 依頼 B-4 と同型 |
| H-115 | **agent の制御チャネルとして使えること**(DUT の UART を空ける) | ◎ | 実測 | いま `reg_probe` は pyserial で UART を掴んでいて、その USART は試験できない。V003 は USART が 1 本 |

### J. 書込・復旧・ISP

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-120 | flash / verify / reset(`ch32rv-probe-<name>` backend として) | △ | 文書 | ch32rv の architecture が P2 で枠を予約済み。**コアはここに困っていない** |
| H-121 | power-off erase / RST erase(unbrick) | ○ | 文書 | LinkE/LinkW 専用機能。SWD pin を GPIO に使った target の復旧 |
| H-122 | BOOT0 ピン制御 | △ | 文書 | V103/V2xx/V3xx/V407/L103 は BOOT0 ピンのみ |
| H-123 | 1200bps touch + `SystemReset_StartMode` の相手 | △ | 文書 | X03x/X315/H417 は SW エントリ可 |
| H-124 | UART ISP / USB ISP の相手 | △ | 文書 | R-17 のオプション経路 |
| H-125 | board 内蔵ライタ(UIAPduino 型)と共存できる | ? | 文書 | R-17。コアの範囲外だがエコシステムの要求 |
| H-126 | firmware 更新が全 OS でできる(UF2) | ○ | 実測 | ~~LinkE は Windows 専用ツールでしか更新できない~~ **訂正(ch32rv `0006` §12): 解消済み**。`ch32rv probe firmware update` が Linux/macOS から LinkE の更新・ダウングレードをできる(0.5.0、2.22 ⇔ 2.13 を実機往復)。**UF2 が望ましい理由は残るが「全 OS で更新できない」という根拠は成り立たない**ので、重みを ◎ → ○ に下げる |

### K. USB / PD / 広帯域周辺

**ここは harness の class で到達範囲が変わる**(§2.5)。「不可」ではなく「どの class なら届くか」で書く。

| ID | 要求 | 重み | 状態 | 根拠 / どの class なら |
|---|---|:--:|---|---|
| H-130 | USB device(FS)の相手 | ○ | 推定 | 本筋は PC。RP2040 は device/host 排他で `Pico-PIO-USB` は PIO を取り合う。**ESP32-S3 は OTG FS を持つので素直** |
| H-131 | **USB PD の相手**(source / sink、profile 列挙、PPS) | ◎ | 実測 | コアは PD を**必ず載せる**(ユーザ指示)。`pd_selftest` 23 check pass だが **negotiation は PD 電源が来るまで未検証**。PD PHY(`FUSB302`)か 2 枚目の X035 が要る |
| H-132 | PD の keepalive / `requestProfile` / `setVoltage` まで回す | ○ | 実測 | todo の `[要機材]` 項目 |
| H-133 | PD 試験時に board を焼かない仕組み | ◎ | 実測 | H-086 と対 |
| H-134 | CAN の相手 | △ | 推定 | トランシーバ + PIO CAN(RP2040)、または ESP32 の TWAI。線は 2 本 |
| H-135 | I2S の相手 | △ | 推定 | pad は 11〜13 本(全 route の和)。PIO / ESP32 の I2S で届く |
| H-136 | **Ethernet の相手** | ○ | 推定 | 11 series が持つ(V203/V208 10M、V307 1G MAC+10M PHY、V317/V407/V467 MAC+10M/100M)。**ESP32-P4 は Ethernet MAC(RMII)を持つので、PHY を両側に置けば harness が peer になれる**。難しいが不可能ではない |
| H-137 | **USB HS device の相手** | ○ | 推定 | V205/V307/V407/X315 が HS。**ESP32-P4 の USB 2.0 HS OTG を host 側に回せば HS で試験できる**。RP2040/S3(FS)では速度が出ない |
| H-138 | **並列バス(FSMC / LTDC / DVP)の相手または観測** | △ | 実測 | pad は FSMC 33〜34、LTDC 34、DVP 15〜19。**ピン数だけなら 56ch class で届く**(§2.5)。難所は本数ではなく**速度**。ESP32-S3 の `LCD_CAM`(DVP 入力 8/16bit ≤40MHz + GDMA + PSRAM)と ESP32-P4 の `PARLIO`(16bit RX/TX + DMA)が候補 |
| H-139 | **SDIO(4bit)の相手** | △ | 推定 | pad は 12〜14。**ピン数は 24ch class で届く**。SD カードを演じられるかは ESP32 の SDIO slave 次第(**要確認**)。SPI モードなら容易 |
| H-140 | QSPI の相手 | △ | 推定 | V205 のみ。pad 24 本 |
| H-141 | 対象外にするものを**明示的に宣言する** | ○ | 文書 | 「やらない」を言うことも要求。ただし §2.5 のとおり**class によって線が動く**ので、宣言は class ごとに |

### L. 成果物・provenance・replay

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-150 | 共通 run ID の下に artifact を集約できる | ○ | 文書 | test-strategy の artifact 一覧 |
| H-151 | golden corpus を content-addressed で保存できる | ○ | 文書 | 同上 |
| H-152 | decoder / libsigrok の version 固定に耐える形式 | ◎ | 文書 | 同上 |
| H-153 | 保存した capture の replay で実機なし回帰 | ◎ | 文書 | 全 PR で回す層 |
| H-154 | **その run が実際に何を検証したかのカバレッジ報告** | ◎ | 願望 | 「配線は利用者の責任」を運用可能にする唯一の仕掛け。skip が静かに増えると緑のまま何も試験していない |
| H-155 | 欠落のある capture を golden へ昇格させない | ○ | 願望 | H-042 の運用側 |

### M. ベンチ運用

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-160 | DUT に依存しない self-test(fixture health preflight) | ◎ | 文書 | 「fixture health の失敗と core の回帰を別の結果にする」 |
| H-161 | 人手なし復旧(watchdog、host からの完全 reset) | ◎ | 実測 | board farm の前提条件。H-106 と対 |
| H-162 | 遠隔ベンチ(別ホストを runner に) | ○ | 文書 | TEST_PLAN の案 b、「本命」 |
| H-163 | fixture inventory にロット番号を記録できる | ○ | 実測 | X035 は**ロット依存で I2C が使えない個体がある**(下から 5 桁目 = 0)。ADC ch3/7/11/15 も同じロットで死ぬ |
| H-164 | 未信頼 fork PR の firmware を常設ハードへ直接焼かない境界 | ○ | 文書 | test-strategy の CI 安全性 |
| H-165 | 複数 DUT / mux | △ | 文書 | Q-043。1 target 専有と排他(§3 C-2) |
| H-166 | 誤配線から board を守る(直列抵抗、過電圧検出) | ○ | 文書 | dut-harness §3.4 |

### N. 他プロジェクトとの共用

| ID | 要求 | 重み | 状態 | 根拠 |
|---|---|:--:|---|---|
| H-170 | `ch32fun` 等から同じ CLI/API で使える | ◎ | 文書 | コアの既定方針 |
| H-171 | **EmbedBench** の模型をそのまま相手役にできる | ◎ | 推定 | 凍結済み IF、純粋 C++11、22 種 3572 行。preload/readback の分担が IF に既にある |
| H-172 | EmbedBench が **mock harness** として同じ socket protocol を喋れる | ◎ | 推定 | 実機なし CI と、host/実機の差分テストが 1 本のテストで書ける |
| H-173 | **I2CDeviceDB** — I2C デバイスの走査・識別の相手役 | ○ | 願望 | test-strategy が「`I2CDeviceDB` の方式を一般化する」と既に書いている |
| H-174 | **TinyGFX** — SPI ディスプレイの相手役、framebuffer 回収 | ○ | 願望 | H-060。EmbedBench の scope 文書も描画検証は TinyGFX 側へ振り分けている |
| H-175 | **host-arduino-core**(`lang-ship:host`)と socket idiom を共有 | ○ | 実測 | EmbedBench が `port: socket://localhost` で既に使っている |
| H-176 | **TinyUSB 上流貢献**の HIL 不足を埋める | ○ | 実測 | todo: PR #3703(X033/X035)の「マージの障害は **HIL で試せていないこと**」。X035 実機はある |
| H-177 | 教育 / ワークショップ用途(driver レス、安価、多人数) | △ | 文書 | generic-probe-design §2 の主要ユースケース |
| H-178 | ベンチ機材そのものの共用(他プロジェクトの ESP32 と USB port を取り合っている) | △ | 実測 | 8 port のうち 7 本を別プロジェクトが使っていた実績 |

---

### O. 他リポジトリの要望を読んで足したもの(2026-09-06)

`ch32rv/docs/data-requests/0006-harness-integration.ja.md` と
`EmbedBench/docs/HARNESS_REQUESTS.ja.md`(`B-001`〜`B-007`)を読んで追加した分。
**上の各カテゴリに属するが、出どころを見えるように別枠にした。**

| ID | 属す | 要求 | 重み | 出どころ |
|---|---|---|:--:|---|
| H-180 | A | **lock のキーを DUT にもできる**。probe 単位では足りない | ◎ | ch32rv `0006` §2.1。**harness と LinkE は別 USB device なので probe 単位の lock は競合しない**のに、同じ DUT を別経路で触る |
| H-181 | A | **core の状態(halt / running)の調停**。セッション protocol に明示的な release / re-acquire | ◎ | ch32rv `0006` §2.2。DMDATA/RTT の polling は running のまま、flash/memory 読み/breakpoint は halt 必須。**agent チャネルが生きている間は halt を伴う操作ができない** |
| H-182 | B | **`caps` に capability の語彙を「可否 + 理由」で持つ** | ◎ | ch32rv `0006` §5。`capabilities --json` が probe×target で可否と理由を出す形が既にある。**H-005 の「contract を揃える」は exit code と envelope だけでは狭い** |
| H-183 | C | **往復の記録を ch32rv と同じ NDJSON 形にする** | ◎ | ch32rv `0006` §6。`--capture` / `--replay` と **divergence 報告**が実在し、offline CI 回帰に使われている。形式を揃えれば道具立てがそのまま効く |
| H-184 | M | **束縛をどの data rev で決めたかを fixture manifest に残す** | ○ | ch32rv `0006` §9。ch32rv は pin された rev、resolver は floating な作業コピーを見る。**2026-09-06 に実際に踏んだ**(data repo が動いて `db-check` が stale、リリース後に気づいた) |
| H-185 | E | **圧縮された時間定数の扱いを決める**(実物値へ設定できる経路 / 圧縮模型を peer に使わない) | ◎ | EmbedBench `B-001`。**模型 6 種が意図的に圧縮**。Modbus は実物 4,010 µs に対し模型 1,500 µs で、9600 baud の 1 文字(1,146 µs)よりわずかに長いだけ。**速すぎて誤動作する**方向で見落としやすい。**EmbedBench 側が実装を引き受けられる** |
| H-186 | B | **`caps` に「模型の時刻要求(`requestWake`)に応えられるか」と分解能**。応えられない環境で要る模型を束縛したら fail closed | ◎ | EmbedBench `B-002`。基底実装は黙って `false` を返すので**静かに劣化する**。EmbedBench で実際に踏んだ(「500 µs 刻みのはずが 1,000 µs 刻み」)。**呼ぶ模型は 12 種** |
| H-187 | E | **再入禁止を probe 環境の実装契約として明示**。保留容量が溢れたら必ず診断 | ◎ | EmbedBench `B-003`。**模型 23 種すべての前提**。実機の割り込みで破られると状態機械が壊れる |
| H-188 | D | **診断語彙とイベント行の形を揃える**。揃えないなら「diff は機能的な署名だけ」と最初に決める | ◎ | EmbedBench `B-004`。準拠キットは語彙を検査していないので**語彙が違っても両方とも準拠**。**後から変えると diff の作り直しになる** |
| H-189 | B | **`caps` に凍結 IF の版**(`embedbench_if: {version, revision}`) | ○ | EmbedBench `B-005`。模型の版とは別に効く |
| H-190 | E | **`channelWrite` を「効果を起こしうる経路」として遅延配送の規律に載せる** | ◎ | EmbedBench §4。**23 種中 6 つが `channelWrite` から `HostPort` を呼ぶ**。effect-free は `reset`/`channelRead`/`dump` の 3 つだけ |
| H-191 | E | **`tests/conformance/` を第 3 の環境の受け入れ門にする** | ○ | EmbedBench §5。既存 2 実装が同じ判定に達することを確認する試験が実在。**新たに考案しなくてよい** |

## 2. 需要側の数字 — DUT は何本・何台を要求するか

**channel 数は harness の制約ではなく、harness class の選択肢**。
ここではコア側の**需要**だけを出す。供給(どの board が何本出せるか)は
`harness-board-survey.ja.md` の役目で、突き合わせは protocol 側で行う。

### 2.1 相手(peer)の数

| 相手 | 数 | 理由 |
|---|---:|---|
| UART peer | 2 | 1 本を試験、もう 1 本を別インスタンスへ |
| I2C target | 2 アドレス | `I2C1` と `I2C2`、または 1 バス上に 2 デバイス |
| SPI target | 1(2 あれば `SPI2` も) | コアの SPI は controller 専用で実デバイス相手が未着手 |
| GPIO | 2〜4 | EXTI を別ポート・別 bit で 2 本、marker、INT/DRDY |
| アナログ入力 | 2〜4 | DAC 出力、VDD、電流 |
| アナログ出力 | 1〜2 | `analogRead` に既知電圧 |

この数は **class が上がっても増えない**。増えるのは **channel 数**のほうで、
「同時に張れるインスタンスの数」と「広帯域周辺に届くか」が変わる。

### 2.2 需要曲線 — channel 数 N で 24 series のうち何 series に届くか

`ch32-device-data` の `index/pinout.csv` から導出。debug 線を含む pad 数。

| 範囲 | 定義 |
|---|---|
| **S1** | 各バス 1 インスタンスずつ(UART/I2C/SPI)+ debug |
| **S2** | **全**バスインスタンス + debug |
| **S3** | S2 + DAC + ADC 2 本 + EXTI 用に別ポート 2 本 |
| **S3+単独最大** | S3 + その series が持つ**広帯域周辺のうち最大の 1 つ**(同時には要らないので単独) |
| **S4** | S3 + 広帯域周辺**すべて**(理論上限) |
| **S5** | パッケージの全 GPIO + debug(チップ丸ごと) |

| N | S1 | S2 | S3 | S3+単独最大 | S4 | S5 |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | **24** | 9 | 8 | 7 | 7 | 0 |
| 12 | **24** | 13 | 11 | 9 | 10 | 0 |
| 16 | **24** | 18 | 15 | 13 | 11 | 0 |
| **20** | **24** | **24** | 20 | 14 | 14 | 6 |
| **24** | **24** | **24** | **24** | 17 | 15 | 7 |
| 32 | **24** | **24** | **24** | 17 | 17 | 7 |
| 48 | **24** | **24** | **24** | 19 | 19 | 11 |
| **56** | **24** | **24** | **24** | **24** | 19 | 15 |
| 80 | **24** | **24** | **24** | **24** | **24** | 21 |

**読みどころ**:

- **8ch で全 24 series の「各バス 1 本ずつ」が成立する。** 最小構成は思ったより小さい。
- **20ch で全 24 series の全バスインスタンス。** 16ch だと 18/24 で、
  届かないのは V303 / V305 / V307 / V317(19 pad)と V407 / V467(17 pad)。
  **16 → 20 の 4 本が、いちばん費用対効果が高い段差**。
- **24ch で S3 が全 series。** ここまでがコアの v1.0 の検証範囲(GPIO/UART/SPI/I2C/ADC/PWM/割込)にほぼ対応する。
- **24 → 48 はほとんど動かない。** 広帯域周辺が 16〜34 pad に固まっていて、
  中途半端な増やし方では届かないため。
- **56ch で「どの広帯域周辺も 1 つは張れる」が全 series 成立する。**
  効いているのは FSMC(33〜34)と LTDC(34)。
- **80ch でチップ丸ごと**(V303/V307/V205 は 82 pad)。

### 2.3 series ごとの需要(抜粋)

| series | S1 | S2 | S3 | 最大単独の広帯域 | S3+それ | S5 |
|---|---:|---:|---:|---|---:|---:|
| V003 / V002 / V004 / V005 / V006 / V007 / M007 | 4〜6 | 4〜6 | 7〜8 | — | 7〜8 | 19〜33 |
| M030 | 5 | 5 | 7 | USBPD 4 | 11 | 37 |
| M103 | 3 | 9 | 11 | CAN1 3 | 14 | 18 |
| X033 / X035 | 5 | 8〜10 | 10〜12 | USB/USBPD 2 | 12〜14 | 20 / 62 |
| X305 / X315 | 6 | 11 | 13 | ARGB 3 | 16 | 57 / 66 |
| V103 | 7 | 14 | 16 | USB 2 | 18 | 53 |
| L103 | 5 | 14 | 16 | CAN1 6 | 22 | 39 |
| V203 / V208 | 7 | 16 | 18 | **ETH 4** | 22 | 53 / 55 |
| V205 | 7 | 16 | 18 | **FSMC 33** | **51** | 82 |
| V303 / V307 | 7 | 19 | 22 | **FSMC 33** | **55** | 82 |
| V305 | 7 | 19 | 22 | **ETH 23** | 45 | 53 |
| V317 | 7 | 19 | 23 | **DVP 16** | 39 | 72 |
| V407 / V467 | 7 | 17 | 19 | **LTDC 34** | 53 | 79 / 78 |

### 2.4 harness class によって線が動く

コア側は class を指定しない(§4)。**どの class を採るかで「できない」の線が動く**、という事実だけを置く。

| class の性質 | コアから見て何が変わるか |
|---|---|
| **連番 16ch** | S2 が 18/24。バス試験の入口としては十分 |
| **連番 20〜24ch** | **S2/S3 が全 series**。コアの v1.0 検証範囲がほぼ埋まる |
| **連番 48ch 級** | S3+単独最大が 19/24。DVP・SDIO・ETH が視野に入る |
| **56ch 以上** | FSMC / LTDC を含めて全 series で広帯域周辺が 1 つ張れる |
| **GPIO matrix(連番不要)** | 配線の自由度が上がる。**pad の並びに合わせて cable を作れる**ので、series ごとのケーブル本数が減る可能性 |
| **大容量バッファ(PSRAM 級)** | burst 専用から**連続キャプチャ**へ。長時間の UART/I2C 監視、起動シーケンス全体の記録 |
| **並列入力ペリフェラル**(`LCD_CAM` / `PARLIO`) | DVP・FSMC の**データバスを本来の道具で捕まえられる**。bit-bang より現実的 |
| **USB HS** | capture の吸い上げが速い。**DUT の USB HS device を HS で相手にできる** |
| **Ethernet MAC 内蔵** | **11 series が持つ Ethernet の peer になれる**(PHY は両側に要る) |

**「難しいが不可能ではない」もの**を、難所の名前つきで並べる:

| 対象 | ピン数 | 本当の難所 |
|---|---:|---|
| DVP(カメラ) | 15〜19 | harness が**カメラ側**を演じる。`LCD_CAM` のパラレル出力が使えるか |
| SDIO 4bit | 12〜14 | タイミング。SD card 側を演じられるか(ESP32 の SDIO slave、**要確認**) |
| Ethernet | 4〜28 | RMII 50 MHz と PHY。MAC を持つ class なら**配線と stack の問題**に落ちる |
| FSMC | 33〜34 | **速度**。SRAM/LCD を演じるので待たせにくい |
| LTDC | 34 | 同上。表示なので落としても気づきにくい = 検証設計が難しい |
| USB HS device | 4 | HS の host 側。**コア側の TinyUSB glue が未着手**でもあるので、順序は後 |

## 3. 相反する要求(protocol 側で裁いてほしいもの)

**どちらか一方を選べとは言わない。**両立させる道があるならそれが最善で、無いなら裁定が要る。

### 3.0 回答が集まった状況(2026-09-06 時点)

他 repo が §3 を読んで材料と回答を返してきたので、**どこが空いているか**を出す。
`C-n` の中身は下表(§3.1)が正本で、ここは状況だけ。

| 相反 | 回答・材料 | 状態 |
|---|---|---|
| **C-1** 模型の置き場 | ベンチ §3「**両立する**」(IF が器で模型が中身) | **事実上解消** |
| **C-2** multi-lane vs 1 target 専有 | ライタ §15(lock が probe 単位なので **lane 単位の排他**が要る) | 材料あり。裁定待ち |
| **C-3** channel 数 vs 被覆 | — | **空いている**(コアが需要曲線 §2.2 を出しただけ) |
| **C-4** capture 帯域 vs control 応答性 | ライタ §15(**DMI 1 往復 471 µs**、flash の bulk 中は他が止まる前例) | 材料あり。裁定待ち |
| **C-5** 論理名 vs SUMP 互換 | ベンチ §3「材料は無い」 | **空いている** |
| **C-6** debug 線を窓に入れる vs 全 ch をアプリへ | — | **空いている** |
| **C-7** 5V target | — | **空いている** |
| **C-8** 模型を積む vs 資源 | ベンチ §3 が**実測**(全 23 種で RAM 6,072 B / `.text` 13.2 KB)+ `devices/tools/measure_footprint.sh` | **事実上解消**(X035 級だけ注意) |
| **C-9** PD 高電圧 vs board 保護 | — | **空いている** |
| **C-10** エミュ vs 実物 | ベンチ §3(**既定はエミュ**+ 第 3 の選択肢 = 両方走らせて差分)/ ライタ §15「材料なし」 | 材料あり |
| **C-11** 配線責任 vs 静かな skip | ベンチ §3(`stats().windows == 0` と同型)/ ライタ §15(`capabilities` の「理由つきで出す」が同型) | 材料あり |
| **C-12** DMI をどの段階で要求するか | ライタ §1 が提起、コアが解き方を提案([harness-probe](harness-probe.ja.md) §4.1) | **裁定待ち(最優先)** |
| **C-13** セッション vs ch32rv backend | ライタ §14-3 が提起、コアが §4 で回答 | 材料あり |

**空いているのは C-3 / C-5 / C-6 / C-7 / C-9 の 5 つ**で、いずれも
**コア側の都合が強い論点**(channel 数・論理名・窓の使い方・電圧・PD 安全)。
他 repo に材料が無いのは自然なので、**裁定側が判断材料を要るなら言ってほしい**。

### 3.1 相反の中身

| # | 対立 | コア側の言い分 |
|---|---|---|
| **C-1** | 「probe は chip を知らない」 vs 「模型を firmware に埋め込む」 | 転送のたびに host へ取りに行く形は SPI/UART で原理的に不可(H-054)。**器で足りるものは器、足りないものだけ模型**、が折衷案。**ベンチの回答: 両立する。対立は言葉の問題**で、probe firmware が知るのは `ebdev::Device` の **10 個の仮想メソッド**であって「24C02 とは何か」ではない。**IF が器で、模型が中身**と読めば `dut-harness-design` §6 は文字どおり成立する |
| **C-2** | multi-lane(`dmibridge`)vs 1 target 専有(harness) | コアが要るのは後者。ただし将来 board farm で前者も要る(H-009 / H-165)。**同じ仕様の別プロファイルで排他**なら困らない。**ライタ §15 の材料**: ch32rv の lock は probe 単位なので、multi-lane では「1 lane を別プロセスが使用中」を表現できない → **lane 単位の排他**が lock のキー設計に効く(H-180 と同根) |
| **C-3** | harness の channel 数 vs 被覆範囲 | **16ch は制約ではなく階層の 1 つ**(§2.2)。16 → 20 の 4 本で S2 が 18/24 → 24/24 になるのが最大の段差。24ch で S3 が全 series。**どの階層を既定にするかは protocol 側の裁定**で、コアは階層ごとの到達範囲を出すところまで |
| **C-4** | capture 帯域 vs control 応答性 | USB FS は ~1 MB/s。flash の bulk とキャプチャが競合する。H-035 の別チャネル化と、動的な degradation の申告。**ライタ §15 の材料**: DMI 1 往復 **471 µs** の実測と、「flash の bulk 転送中は他の往復が止まる」構造が**前例として使える** |
| **C-5** | 論理名を probe に持たせない(H-010) vs SUMP/sigrok 互換 | SUMP はチャネル名を持つ。**名前の付与を出力段(host)でやる**なら両立するか |
| **C-6** | debug 線を窓に入れる(H-047)vs 16ch 全部をアプリ信号に使う | dut-harness §5 の案 A なら幅 14/16 で出し入れできる。コアの論理信号は 13 本なので**両立する** |
| **C-7** | 5V target 対応(H-085)vs RP2040 の 3.3V | 分圧かレベル変換。ベンチに 5V 動作の board があるかは**未確認** |
| **C-8** | 模型を積む(H-015)vs RAM / flash / PIO 命令メモリ | **ベンチが実測して事実上解消**: 全 23 種で **RAM 6,072 byte / `.text` 13,261 byte(Cortex-M0+)/ 15,579 byte(RV32IMAC)**。RP2040 の 264 KB に対しては誤差。**RAM の 77% は SD カード 1 つ**でブロック数に線形なので、`harness-board-survey` が挙げる **X035(RAM 20 KB 級)では SD カードだけで 23%** → そこはビルド時プロファイル(H-057)が効く |
| **C-9** | PD の高電圧試験(H-132)vs board 保護(H-086 / H-133) | **安全側を既定にしてほしい**。5V 以外は明示的な opt-in |
| **C-10** | エミュ模型 vs ベンチ上の実物デバイス(H-020) | エミュは障害注入ができ、実物は忠実度が高い。**ベンチの回答: 既定はエミュを推す**(理由は忠実度ではなく**再現性と障害注入**。`unit_faulty_model` は無応答・data NACK・途中で切れる読み出し・間欠故障を決定的に起こせる)。加えて**第 3 の選択肢** — **両方を同じテストで走らせて差分を取ると模型の検証になる**。実物が載っているベンチでしかできないので、実物の価値はそこにある |
| **C-11** | 「配線は利用者の責任」vs 静かな skip | H-154 のカバレッジ報告が無いと成立しない。**報告は要求として強い**。**同型の前例が 2 つ**: ベンチの `stats().windows == 0`(`runBegin` を呼んでいない)、ライタの `capabilities`(**「できない」を理由つきで出す**ことで静かに落ちるのを防ぐ) |
| **C-12** | **DMI 能力をどの段階で要求するか** | ch32rv `0006` §1 の指摘。**`harness-probe` は `W` を最後に置き、`harness-testing` は DMI を前提にしている。**こちらの解き方は「**DMI と flash を分ける**」で、コアが早く欲しいのは `L` + **`M`(lane 0 本 + DMI read/write、flash アルゴリズム無し)**、`W` は最後のまま([harness-probe](harness-probe.ja.md) §4.1)。**裁定は protocol 側** |
| **C-13** | **H-030(セッション)vs H-120(ch32rv を backend に残す)** | ch32rv `0006` §14-3。H-030 の根拠だった `reg_probe` の 30〜130 秒は、まさに **ch32rv の CLI を毎回起動している**ところ。**harness が DMI(`M` 段)を持てば経路から外れて張力は消える**。コア側の回答は §4 の「どちらでもよい」欄へ |

---

## 4. コアとして「どちらでもよい」もの

裁量を返す。protocol 側の都合で決めてよい。

| 論点 | コアの立場 |
|---|---|
| 既定の transport(`serial://` か `socket://` か) | どちらでも。**両方あることが要求**(H-031) |
| `caps` の直列化形式(JSON / CBOR / 独自) | どちらでも。**machine-readable であることが要求**(H-002) |
| 名前(DUT harness / bench probe / DUT scope …) | どちらでも |
| ~~模型の版付け規則をどのリポジトリが採番するか~~ | **解決(B-006): EmbedBench が採番する。** 版が数えるのは**観測可能な振る舞い**であって、ソースではない(コメント・整形・内部実装は据え置き、応答内容・時間定数・診断文言・channel 割り当ては繰り上げ)。模型ごとに独立 |
| 役割の語彙(`i2c_target` / `uart_peer` …)の正本 | どちらでも。**ただし論理信号名の一次資料はコア側にある** — `research/signal-name-normalization.ja.md`(R-19)。**正規化が要るのは V003 / X033 / X035 の 3 series だけ**で、残りは最初から canonical 形 |
| board / class(RP2040-Zero / Pico / Pico 2 / RP2350B / ESP32-S3 / **ESP32-P4** / X03x) | **probe 側の判断に従う。** コアは §2.2 の需要曲線を出すところまで。ただし **16 → 20ch の 4 本が最大の段差**であることは伝えたい |
| 1 台に載せるか 2 台で分業するか | どちらでも。分業なら共有 trigger での時間軸較正(H-048)が要求になる |
| capture unit を 1 個にするか 2 個にするか | `caps` で申告されるなら(H-016)どちらでも |
| CLI の名前・サブコマンド構造 | ch32rv の contract に揃っていれば何でも(H-005) |
| **ch32rv に持続セッションの口を足すか**(ch32rv `0006` §14-3 の問い) | **現時点では要求にしない。** `reg_probe` の 30〜130 秒は harness が DMI(`M` 段)を持てば消える。**ただし DMI 段が遅れるなら、もっと安い代案がある** — `read` が**複数レンジを 1 回の起動で**受けられれば、散在するレジスタ 200〜400 本のプロセス起動が 1 回に畳める。セッション化より小さい変更で同じ効果が出るので、**先にこちらを検討してほしい** |

---

## 5. まだ要求になっていない気づき(将来枠)

実現性も需要も確かめていない。**枠だけ残す。**

| # | 気づき |
|---|---|
| F-1 | **可変電圧の target**(1.8V / 2.5V 動作の CH32 が出たとき)。いまは 3.3V/5V しか考えていない |
| F-2 | **温度を振っての再測定**。HSI トリムの温度依存(H-090) |
| F-3 | **ノイズ・グリッチ注入**による堅牢性試験。EMC ではなく論理的な誤動作の再現 |
| F-4 | **複数 harness の同期**。DUT 2 枚が通信する構成(CAN、I2C マルチマスタ) |
| F-5 | **ブラウザから実行**。`generic-probe-design` §8-4 の Web ライタと同じ層で、テストも回せるか |
| F-6 | **harness 自身の校正**。時間軸・ADC・電流の絶対値を、外部の基準に対して合わせる手順 |
| F-7 | **DUT 側 Arduino ライブラリ**。スケッチから harness に marker やイベントを直接出せると、passive 試験の解像度が上がる。ただし「無改造のスケッチで試験する」という原則と衝突する |
| F-8 | **エラッタの再現**。X035 のロット依存 I2C(H-163)のように、個体差を試験系がどう扱うか |
| F-9 | **消費電力の回帰**。sleep 電流を数値で追い、悪化を検出する(H-083 の先) |
| F-10 | **`millis()` の 49.7 日 wraparound** を実機で確認する手段。いまは test-only seed で近傍へ飛ばす方針 |

---

## 6. 他リポジトリの指摘への対応(相手が線を引くための表)

ch32rv `0006` §11 が「相手の文書が更新されたら解消した項目に取り消し線を引く」としているので、
**何をどう反映したかを一覧にする**。

### 6.1 ch32rv `0006` からの指摘

| 指摘 | 対応 | 反映先 |
|---|---|---|
| §1 2 文書の前提がずれている | **受領。指摘は正しい。**「DMI と flash を分ける」で解いた案を出した。**裁定は protocol 側** | [harness-probe](harness-probe.ja.md) §4.1、**C-12** |
| §2.1 lock のキーが probe では足りない | **受領。要求として追加** | **H-180** |
| §2.2 core の状態(halt/running)にも排他がある | **受領。要求として追加** | **H-181** |
| §2.3「ch32rv = CLI 1 回 1 操作」は半分だけ正しい | **訂正済み**(gdb server / monitor がセッション型) | [harness-testing](harness-testing.ja.md) §3.1 |
| §3 実測値(DMI 1 往復 **471 µs**) | **見積りを実測に差し替え。** 結論は変わらず | [harness-testing](harness-testing.ja.md) §9.2 / §4.2 |
| §4.1 `ProbeService` はコードに存在しない | **訂正済み** | [harness-probe](harness-probe.ja.md) §1 |
| §5 contract は exit code と envelope より広い | **受領**(capability の語彙、可否 + 理由) | **H-182** |
| §6 mock 段 3 は実在(`--capture` / `--replay`) | **受領**(NDJSON 形を揃える) | **H-183** |
| §7 `AttachChip` が GPR `s1` を破壊 | **受領**(監査対象に GPR / CSR) | **H-101 / H-108** |
| §8 DMI へ移す代償(debug 線を占有) | **受領。利点表に代償の行を足した** | [harness-testing](harness-testing.ja.md) §4.2 |
| §9 データ rev の固定 | **受領。要求として追加** | **H-184** |
| §12 H-126 の根拠が古い | **訂正済み。重みを ◎ → ○ に下げた** | **H-126** |
| §12 H-002 の根拠が古い | **根拠を更新。要求は有効のまま** | **H-002** |
| §14-1 書込後の read がいつ確定するか | **受領。要求として追加** | **H-109** |
| §14-3 ch32rv に持続セッションの口が要るか | **回答した。現時点では要求にしない。**より安い代案(複数レンジ read)を提示 | §4 の表 / **C-13** |

### 6.2 EmbedBench `HARNESS_REQUESTS` からの依頼

| ID | 対応 | 反映先 |
|---|---|---|
| **B-001** 圧縮された時間定数 | **受領。最優先として追加。**(a)実物値へ設定できる経路を推す — **実装を引き受けてもらえるとのことなので、その方向で** | **H-185** |
| **B-002** `requestWake` の宣言と fail closed | **受領** | **H-186** |
| **B-003** 再入禁止の明示と保留容量の診断 | **受領** | **H-187** |
| **B-004** 診断語彙とイベント行 | **受領。「先に決まっていると嬉しいのはこれだけ」という優先度も受け取った** | **H-188** |
| **B-005** `caps` に凍結 IF の版 | **受領** | **H-189** |
| **B-006** 模型の版は EmbedBench が採番 | **合意。** §4 の「どちらでもよい」から外し、決定として扱う | §4 |
| **B-007** 既定プロファイルの判断材料 | **受領。`unit_faulty_model` の推挙も含めて** | H-057 の材料 |
| §1 数値の訂正(23 種 / 2,996 行 / 464 行) | **訂正済み。以後 `FACTS.ja.md` を引く** | [harness-probe](harness-probe.ja.md) §6.2 |
| §4 `channelWrite` は effect-free ではない | **訂正済み。こちらの表の誤り** | [harness-testing](harness-testing.ja.md) §9.4、**H-190** |
| §8 環境実装は probe 側へ | **合意。未決から外す** | [harness-probe](harness-probe.ja.md) §6.4 |
| §9 差分に乗るノイズ(§0-5 が強すぎる) | **訂正済み。**「バグ候補になりうる」へ弱め、ノイズ源を明記 | [harness-probe](harness-probe.ja.md) §6.3 |
| C-1 への回答(両立する) | **受領。** IF が器で模型が中身、という読みを採る | C-1 |
| C-8 への回答(実測 RAM 6,072 B / .text 13.2 KB) | **受領。RP2040 級では問題にならない。** ただし X035(RAM 20 KB 級)では SD カードだけで 23% | C-8 |
| C-10 への回答(既定はエミュ + 第 3 の選択肢) | **受領。**「両方を同じテストで走らせて差分を取る = 模型の検証」は**こちらに無かった発想** | C-10 |
| §5 段階との関係(`L` / `WL` を妨げない) | **受領。** 急ぐのは B-004 だけ、という整理に同意 | C-12 |
| §3 C-5 / C-11 への補足 | **受領。** C-11 は `stats().windows == 0`(`runBegin` を呼んでいない)と同型という指摘 | C-11 |

### 6.3 まだ返せていないもの

| 相手 | 論点 | 状態 |
|---|---|---|
| ch32rv | §10 の提供物(attach 時の USB 往復 capture 6 family) | **欲しい。**[harness-probe](harness-probe.ja.md) §7-2 の共同実験の材料になる |
| EmbedBench | §7 の提供物(SD カードのプリセット 7 枚) | **欲しい。** 巡回クラスタ鎖はコアの FAT ドライバ試験にそのまま使える。**ただしコアに FAT ドライバはまだ無い** |
| EmbedBench | B-004 のログ形式 | **こちらから形を出す番。** `test-strategy` の 2 層判定を diff に適用する形で提案する |

---

## 7. 次にコア側でやること(protocol への依頼ではない)

- 論理信号名 12(+`MCO`)を確定して文書に固定する(H-049 の前提)
- Q-050(LA channel / connector / 電源)の判断を harness の結論が出るまで保留する
- 5V 動作の board がベンチにあるかを確認する(C-7)
- `bench.json` を配線表の置き場所として設計する
- [R-17](research/upload-programmers.ja.md) の Tier 表に harness の枠を用意する
