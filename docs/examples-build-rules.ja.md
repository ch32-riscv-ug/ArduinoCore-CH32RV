# examplesのビルド規則

- 状態: **提案**(ADR未起票)
- 文書基準日: 2026-08-29
- 関連: [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md):266(profileはTier A/Bのみ)、
  [ADR-0013](adr/0013-bundled-libraries.ja.md)(bundled libraries)、
  [board-layer-rules.ja.md](board-layer-rules.ja.md)(§4-1でexamplesのpin方針を決めた)

## 0. 前提

リリース前で実ユーザがいない。破壊的変更をしてよい。

## 1. 問題

examplesだけが、この repository の2つのビルド機構のどちらにも乗っていない。

| 対象 | ビルド方法 | ボード | スキップ規則 |
|---|---|---|---|
| `libraries/*/examples/*` (23) | `tests/compile/compile_examples.py` | **ハードコード2枚**(X035/V003) | **手書き`SKIP` dict** |
| `tests/sketches/basic/*` (16) | 生成`sketch.yaml` + profile build | Tier A/B (6枚) | `REQUIREMENTS`(flash/RAM) |
| core + libraries | `tests/compile` matrix | 全122 pnum | — |

`SKIP` dictは **「何が落ちるか」** を人が書く形になっている。
USB・USBPD・DACのような特殊ペリフェラルのexampleが増えると、
(example × board) の組合せぶん手書きが要り、必ずドリフトする。
しかも「なぜ落ちるか」がdictのコメント頼りで、利用者からは見えない。

## 2. 参考: host-arduino-core のやり方(2026-08-29 確認)

- examplesは**1つずつ`sketch.yaml`を持つ**。FQBN(メニュー値込み)、platformバージョン、
  `libraries:`依存まで固定する
- `scripts/bump_version.py`が`libraries/Host/examples/*/*/sketch.yaml`を一括更新する
  (CH32の`tests/sketches/sync_profiles.py`と同じ「生成ブロック同期」)
- examplesは必要な土台ごとにディレクトリ分けされている(`Plane/`、`SDL2/`)
- examplesはリリースZIP同梱の手元実行用

**採るところ**: sketch.yamlがビルド対象の宣言であること、生成で同期すること。
**CH32で違うところ**: CH32はlibrary自体が既にグルーピングなので、ディレクトリ分けは不要。

## 3. 決定

### 3-1. 要件はexampleの`.ino`が宣言する (**宣言部は実装済み / sketch.yaml生成は未着手**)

`compile_examples.py`の`BOARDS`と`SKIP`を**両方とも消す**。
代わりに、各exampleが`.ino`の冒頭で**何を要求するか**を1行で宣言する。

```c
/* requires: USBFS */
/* requires: USBPD, flash=32K */
```

- 宣言が無いexampleは**どのボードでも通る**扱い。Blink等はこれで、注記の負担が無い
- capability tokenは変数名ではなく **`CH32_CLKEN_<name>_ADDR` の `<name>`**
- `flash=` / `ram=` は下限。`sync_profiles.py`の既存`REQUIREMENTS`と同じ意味

**`.ino`に置く理由**: examplesは利用者に配られる。
「なぜ自分のボードでこのexampleが出てこないのか」が、exampleを開けば分かる。
生成器の中央表に置くとCIからは同じでも、利用者からは見えない。

**`sketch.yaml`は全文生成**とする。要件が`.ino`へ移ったので手書き部分が残らない。
`tests/sketches`側は「ヘッダコメントは手書き、`profiles:`ブロックだけ生成」だが、
examplesはヘッダも要らないので丸ごと生成でよい。
生成物はコミットする(利用者が`--profile`で使い、リリースZIPに載るため)。
CIは`sync_profiles.py --check`と同じ形で同期を検証する。

### 3-2. capabilityの出所は新規に要らない (**実装済み**)

生成済みvariantヘッダの `CH32_CLKEN_<PERIPH>_ADDR` がそのままcapability tokenになっている
(実測86種)。device-data由来で、EVTツリー不要、repository内で完結する。
USBPDライブラリは既に`#ifdef CH32_CLKEN_USBPD_ADDR`でこれを使っている
(`libraries/USBPD/src/usbpd_hw.h:31`)。

実測(`variants/*/pins_arduino.h`から集計):

```
USBPD : X033 X035 X305 X315 V205 L103 M103 M030
USBFS : V103 V203 V205 V208 V303 V305 V307 V317 X033 X035 L103 M103 M030
USBHS : V203 V205 V208 V303 V305 V307 V317 X305 X315
DAC   : V103 V203 V208 V303 V305 V307 V317 V407 V467
```

### 3-3. 1つの宣言から2つを生成する

[TEST_PLAN.ja.md:266-268](../tests/TEST_PLAN.ja.md) が既に決めている:

> `sketch.yaml`のprofileはTier AとBにだけ作ります。Tier C/Dは`tests/compile`のmatrixが見ます。

profileは**「誰かが実機で動かす」約束**なので、実機の無いボードにprofileを作ると嘘になる。
そこで宣言は1つのまま、出力を2つに分ける:

| 出力 | 意味 | 対象 | 絞り込み |
|---|---|---|---|
| `sketch.yaml`の`profiles:` | **実機で動かせる** | Tier A/B (6枚) | 要件を満たすもののみ |
| compile sweep | **少なくともbuildは通る** | **全24 series × `pnum=ANY`** | 同じ要件で絞る |

ドリフトしない。TEST_PLANの不変条件も保たれる。

**sweepの粒度はseriesあたり1ボード**(2026-08-29決定)。
`pnum`はvariantを変えないので、pnum全122を回してもexampleのビルド結果は変わらない。
seriesが変われば pad・peripheral・vector table・ISA が変わるので、そこが本当の軸。

**カバレッジの実測**: Tier A/B(X035/V003/V203/L103/V103/V307)は86 peripheral中67を触れる。
残り19はすべて`[compile only]`ボード側に集中する:
ADC4 / ARGB / I3C / LTDC / OPCM / PIOC / PSRAM / QSPI1 / USART5-10 /
USBHS1 / USBHS2 / USBPD0 / USBPD1 / USBSS
(V407 / V467 / X305 / X315 / V205 / M030)。
**これらはprofileを作れない**(実機が無い、または未発売)ので、compile sweepだけが見る。

### 3-4. CI時間は買う。失敗メッセージに投資する (**実装済み**)

実測: 1ビルド **約2.2秒**(初回のみ10秒。`Blink`、ローカル、直列)。
23 example × 24 series = **552 build ≒ 20分**(直列)。

これはGitHub Actionsで許容する(2026-08-29決定)。
運用は**失敗通知を見たときだけ確認する**形なので、壁時計時間を削るより
**失敗出力が単体で読めること**に投資する。

スキップは既に「Skips are printed, never silent」を守っている。維持したうえで、
出力するのは**宣言された要件**であって失敗の症状ではない:

```
== TinyUSB/CDCEcho: skipped on CH32V003 - requires USBFS, series has none
```

失敗時も同じ原則で、「どのexampleが」「どのseriesで」「宣言した要件は何で」
「実際に何が起きたか」が1つのブロックに揃っていること。
ログを遡らないと分からない形にしない。

### 3-5. 簡易テストとGitHubのチェックを分ける (**実装済み**)

1段では両立しない。ローカルは速さ、GitHubは網羅が要る(2026-08-29決定)。

| 段 | 何を回すか | ボード | 目安 | いつ |
|---|---|---|---|---|
| **簡易**(ローカル) | examples全部 | Tier A 2枚(X035/V003) | 約1分 | コミット前 |
| **sweep**(GitHub) | examples全部 | 全24 series | 約20分 | push / PR |

- 既存の語彙に乗せる。`tests/conftest.py:9`が既に
  `pytest -m "not slow"`で「multi-minuteのcompile sweepを飛ばす」を提供している。
  sweepは新しいマーカー(`sweep`等)で**既定から外す**
- **compile-matrixジョブに相乗りさせない**。
  現在は「ついでに約1分」で載っている(`.github/workflows/ci.yml:103-109`)が、
  20分になると**OS 3面 × 20分**になる。専用jobへ出す
- sweepは**Linuxのみ**でよい。ここで見たいのはターゲットのCコードであって、
  ホストOS差ではない。ホストOS差はcompile-matrix側が既に3面で見ている

### 3-6. `sketch.yaml`が無いexampleは**失敗**にする

「ボードのスキップ」と「exampleごと消える」は別。
sketch.yamlを書き忘れたexampleが黙ってカバレッジ0になるのを防ぐ。
`test_each_library_has_at_least_one_example`と同じ考え方。

## 3.5. 実装の記録 (2026-08-29)

`tests/compile/compile_examples.py` を書き換えた。

- **`SKIP` dictを削除した。** 唯一の項目だった `CH32/PrintFormatting` は、
  `.ino`のヘッダに `requires: flash=32K` と書くようになった。
  結果として **V003 / V002 / V006 の3 series**(ANYが16K)を自動でスキップする。
  手書きのボード名はもう無い
- `series_capabilities()` が `variants/*/pins_arduino.h` から
  `CH32_CLKEN_<X>_ADDR` を集める。`series_limits()` が `boards.txt` の
  ANYエントリから flash/RAM を読む
- `requirements()` が `.ino` から `/* requires: USBFS, flash=32K */` を読む。
  宣言の無いexampleは全ボード対象
- `run(work, boards)` で board集合を差し替える。`FAST_BOARDS` は従来の2枚、
  `all_boards()` は `boards.txt` の全24 series
- 失敗時の出力に**宣言した要件とbuild propertiesを併記**する。
  失敗通知だけ見る運用なので、ログを遡らずに原因が読めること

テスト側:

- `tests/compile/test_examples.py` — 従来どおり2枚(`slow`)
- `tests/compile/test_examples_sweep.py` — 全24 series(`slow` + 新marker `sweep`)
- `sweep`は**opt-in**。`conftest.py`に`--sweep`を追加し、
  付けないと`pytest_collection_modifyitems`がskipする。
  20分を待っている人の前で走らせない

`sketch.yaml`の生成は`tests/sketches/sync_profiles.py`を拡張した。

- 共有部を`tests/sketch_requirements.py`へ切り出し、
  `compile_examples.py`(何をビルドするか)と`sync_profiles.py`(sketch.yamlに何を書くか)
  の**両方が同じ答えを使う**ようにした。`conftest.py`の
  「fixtureでない共有コードは`loader.py`の隣」という規約に沿う。
  `tests/`直下は`unit/test_tests_layout.py`の`ROOT_ALLOWED`で名前列挙されており
  (「3つ目を足すのは決定であるべき」)、理由を書いて明示的に追加した:
  **2つのカテゴリが食い違ってはいけない共有物で、どちらのカテゴリの持ち物でもない**
- `tests/sketches/**`は従来どおりヘッダコメント+生成ブロック。
  **既存の16ファイルは1バイトも変わらない**(リファクタが挙動を変えていない証拠)
- `libraries/*/examples/*/sketch.yaml`は**全文生成**。無ければ作る
- 満たせないboardは理由を書いて外す:
  `# tier A CH32V003: omitted, requires 32 KB flash, CH32V003 ANY has 16 KB`
- 未知のcapability名は**エラー**にする。タイポが「どのseriesも持たない」と読まれると
  全boardスキップで**何もビルドせず緑**になるため

### 落とし穴: sketch.yamlがあると`--fqbn`が無視される

examplesに`sketch.yaml`を置いた瞬間、`compile_examples.py`が全滅した。
**sketch.yamlがあるとarduino-cliはprofileの`platform_index_url`から
platformを解決し、`--fqbn`を無視する**。開発ツリーではそのURLはまだ404なので、
`Platform 'ch32-riscv-ug:ch32v' not found`で落ちる。

これは既知で、`tests/sketches/stage.py`のdocstringに書かれていた。
対策も同じで、**ビルド前にsketch.yamlを除いたコピーへstageする**。
`compile_examples.py`も`stage_sketch()`を使うようにした。

利用者側では問題にならない(Board Manager経由でindexが公開されていれば解決できる)が、
**ローカルのplatformに対してexampleをビルドする経路は必ずstageが要る**。

**まだやっていない**: CIジョブの分離は入れた(`example-sweep`)が、
`sync_profiles.py --check`がexamplesも見るようになったぶん、
`test_sketch_profiles.py`の説明文が古い。

## 4. 未解決

- **`requires:`の書式**。上記は`/* requires: USBFS, flash=32K */`の1行案。
  複数行にするか、`.ino`のどこに置くか(1行目固定か、任意位置か)は未決
- TinyUSBに`library.properties`が無い(examplesもまだ無い)。
  CDC glueが入るときがこの規則の最初の本番になる
- `USBPD/ListProfiles`は現在フレーム論理だけなので全ボードでbuildできる。
  ハードウェアドライバが入ると`requires: USBPD`が要る
- capability tokenを`CH32_CLKEN_*`に依存させると、
  [board-layer-rules.ja.md](board-layer-rules.ja.md)§5の
  「variantから生アドレスを隠す」案と衝突する。隠すなら別の参照経路が要る
- sweepとprofileでpnumを変えるか(現在はどちらも`ANY`)
