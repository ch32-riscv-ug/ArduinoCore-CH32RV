# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

対象: `ch32-riscv-ug:ch32v`(ArduinoCore-CH32)。
Arduino Board Manager経由で配布するArduino coreとして、**利用者が受け取るもの**が動くことを検証します。

> **この文書は提案です。** 特に「検証boardの絞り込み」はmaintainerの承認を経ていません
> ([承認状態 A-3](../docs/approval-status.ja.md))。実装済みのtoolやCIが動いていることは、
> ここに書いた方針が採用されたことを意味しません。

## テスト方針

テストはソフトウェアで環境を完全に制御できるかどうかで2種類に分けます。

**自動テスト**はCIまたはローカルで人の操作なしに実行します。
入力はすべてプログラムで生成し、期待される出力はすべてアサーションで検証します。

**手動テスト**は環境をソフトウェアで完全に制御できない場合に使います。
「自動化が面倒だから」ではなく、配線の変更・ロジックアナライザ・目視のように
**人の操作か専用計測器が本質的に必要**な場合に限ります。

この2つに加えて、このrepositoryには制約が3つあります。テスト設計はこの3つから決まっています。

| 制約 | 影響 |
|---|---|
| **Board Manager配布物である** | 検証対象はrepository treeではなく**配布アーカイブ**。install経路そのものがテスト対象 |
| **24 series / 122 part number** | 全部を実機では回せない。**ハードウェア差分軸**で代表を選ぶ |
| **WCH-LinkEを同時に1台しか接続できない** | 実機テストは**逐次**。常時接続するboardを絞る |

---

## Board Manager配布物としてのテスト

ライブラリやcoreをBoard Managerで配るときの典型的な失敗は、
**repositoryでは通るのにinstallすると壊れる**ことです。原因はほぼ次のどれかで、
どれも「repository treeでcompileする」テストでは絶対に検出できません。

| 失敗の型 | 具体例 | 対策 |
|---|---|---|
| アーカイブに入っていないファイルを参照している | `variants/`のld scriptを忘れる、`libraries/`が入っていない | `gen_index.py`の`PLATFORM_ENTRIES`を唯一の定義とし、**そこから作ったアーカイブでcompileする** |
| 開発時のpath上書きに依存している | `compiler.path=`が空(PATH探索)のまま出荷される | packagingで`{runtime.tools.…}`へ書き換え、**上書きを一切渡さずにcompile**する |
| ツールが降ってこない / 動かない | probe-rsが`.tar.xz`で、arduino-cliが展開できない | installして**実際に`--version`を実行**する |
| indexのmetadataが実体とずれる | boardが1個しか出ない、`version`がplatform.txtと違う | `boards`は`boards.txt`から、`version`は`platform.txt`から生成する |
| 古いversionがinstallできなくなる | index再生成で過去versionが消える | indexは**append-only**。`gen_index.py --merge` |
| 特定OSだけ壊れる | Windowsでsymlink、pathの区切り、`.exe` | install検証をlinux/macOS/Windowsのmatrixで回す |

### 検証マトリクス

| 配布物 | 何を確認するか | どこで |
|---|---|---|
| platform archive | 単一rootフォルダ、必須entryが揃う、checksum/size一致 | `tools/index/gen_index.py` |
| platform archive | **上書きなしでcompileが通る** | `tools/index/test_install.sh` |
| package index | `version`が`platform.txt`と一致 | `gen_index.py`(不一致はエラー) |
| package index | `boards`が`boards.txt`の全24 boardと一致 | `gen_index.py` |
| package index | append-only(過去versionが残る) | `gen_index.py --merge` |
| toolchain tool | installされ、compilerとして解決される | `test_install.sh` |
| probe-rs tool | installされ、`--version`が動く | `test_install.sh` |
| upload経路 | `arduino-cli upload --programmer wch-link`が通る | `tests/manual/smoke.py`(実機) |
| profile経路 | `sketch.yaml`の`platform_index_url` + `programmer:`で動く | `tests/sketches/`(実機) |
| 3 OS | 上記すべて | `.github/workflows/ci.yml`の`install-test` matrix |

### 承認されていないもの

配布経路の実装は入っていますが、**外へ出す判断は未承認**です
([承認状態](../docs/approval-status.ja.md))。

- probe-rsのWindowsアーカイブ再ホスト(A-2)。indexが指すURLは未公開で、現時点では404
- package index自体の公開。`release.yml`は未実行

### まだ埋まっていない穴

- [ ] **upgrade経路**: 0.0.1をinstallした環境へ0.0.2を上書きinstallする検証
- [ ] `test_install.sh`のcompile対象が`CH32V006`のBlink 1本。**Tier Aのboardでacceptance sketchを** compileすべき
- [ ] `libraries/`(SPI / Wire)を同梱したあと、installした状態で`#include <SPI.h>`が解決されるか
- [ ] アーカイブに`tests/`や`docs/`が混入していないことの明示的な確認

---

## テストの種類とディレクトリ

```text
tests/
  sketches/    自動 — sketch単位のAPIテスト(pytest + pytest-embedded)。実機/buildのみ両対応
  compile/     自動 — 全122 part numberのcompile matrixとサイズ回帰
  startup/     自動 — 統合crt0とEVT startupのELF等価性
  sizebench/   自動 — newlibのサイズ計測
  manual/      手動 — 実機と人の操作が要るもの、および実機を扱う便利tool
tools/index/   自動 — package index生成とclean install検証
```

`sketches/`は**1 caseにつき1ディレクトリ**です。

```text
tests/sketches/<category>/<case>/
  <case>.ino
  sketch.yaml        profile = board。tests/sketches/sync_profiles.pyが生成
  test_<case>.py     dut fixtureへのexpect
```

`manual/`には`test_`プレフィックスを**付けません**。`pytest`を引数なしで回したときに
実機が要るものが混ざらないようにするためです。明示的に指定して実行します。

```text
tests/manual/
  chip_info.py       tool — いま何が繋がっているか(probe / chip / port / FQBN / Serial pin)
  uart_scan.py       tool — boardがどのUSART routeを配線しているか特定する
  smoke.py           tool — 出荷経路でcompile→upload→UART確認(Milestone 1のacceptance)
  bench.json               この作業台の配線記録
  <case>/<case>.py         手動テスト本体(必要なら<case>.ino + sketch.yaml)
```

---

## 検証boardの絞り込み(**提案・未承認**)

> 対象boardの確定は[Q-001](../docs/open-questions.ja.md)で未決です。
> どのboardを常時接続にするかは所有実機と運用の都合で決まるもので、
> ここに書いた階層は**そこから逆算した提案**にすぎません
> ([承認状態 A-3](../docs/approval-status.ja.md))。

24 series・122 part numberを全部実機で回すことはできません。
**compileは全部**、**実機はハードウェア差分軸の代表だけ**にします。

### ハードウェア差分軸

coreの実装が実際に分岐する軸だけを数えると6つです。

| 軸 | 値 | どこで効くか |
|---|---|---|
| ISA | `rv32ec` / `rv32emc` / `rv32imc` / `rv32imac` / `rv32imafc` | compiler、multilib、atomic有無 |
| GPIOポート幅 | 8 / 16 / 24 bit | `CFGLR`のみ / `+CFGHR` / `+CFGXR`・`BSXR` |
| SysTick | 32 / 64 bit | `millis()`のカウンタ読み出し |
| flash wait state | 0 / 1 / 2 | クロック引き上げ**前**に設定しないとハングする |
| vector table形式 | ジャンプ表 (V103) / アドレス表 | crt0とリンカ配置 |
| EXTIのvector分割 | `EXTI7_0`+`EXTI15_8` / `EXTI0..4`+`9_5`+`15_10` | `attachInterrupt()`のISR生成 |

### 階層

| Tier | Board | 実行頻度 | 選んだ理由 |
|---|---|---|---|
| **A** | **CH32X035** | 全PR相当(手元)＋release | 主対象と聞いている。24 bitポート・48 MHz・wait state 2という最も特殊な組み合わせ。USB-PDの実装先 |
| **A** | **CH32V003** | 同上 | 反対の極。`rv32ec`(M/A/Cなし)・8 bitポート・16 K flash・32 bit SysTick。ここが通れば下限が保証される |
| **B** | CH32V203 | 週次 / release前 | 最も普及しているV3系。`rv32imac` + 16 bitポート + 64 bit SysTick |
| **B** | CH32L103 | 週次 / release前 | V4C系。低消費電力系のクロック経路 |
| **B** | CH32V103 | 週次 / release前 | **vector tableがジャンプ表なのはこのfamilyだけ** |
| **B** | CH32V307 | 週次 / release前 | `rv32imafc`(F拡張)。手元で最大のpart |
| **C** | CH32V006 / CH32V205 / CH32X315 | 随時(月次〜) | Tier A+Bで埋まらない軸: `rv32emc`・`rv32imc`・20 MHz/wait state 1 |
| **D** | 残り15 series | compileのみ | 上のいずれかと差分軸が完全に一致する |

Tier A+Bで、ISA以外の5軸はすべて両方の値が踏まれます。ISAの`rv32emc`と`rv32imc`だけが
Tier Cに落ちますが、これはcompiler側の差でありcoreのCコードは同一です。

`sketch.yaml`のprofileはTier AとBにだけ作ります。
**profileがあるということは誰かが実機で回すという約束**なので、回せないprofileは無いほうがましです。
Tier C/Dは`tests/compile`のmatrixが見ます(profileは不要)。

Board追加時は[`tests/sketches/sync_profiles.py`](sketches/sync_profiles.py)の`BOARDS`だけを直し、
`uv run tests/sketches/sync_profiles.py`で全`sketch.yaml`を再生成します。

---

## ペリフェラル別の検証方法

コストの安い順に4つあります。**上から順に検討し、下は上で無理なときだけ使います。**

| # | 方法 | コスト | 何が分かるか | 何が分からないか |
|---|---|---|---|---|
| 1 | **セルフチェック** — MCUが自分で確かめて結果をSerialに出す | ほぼゼロ。配線不要 | APIが値を返す、範囲に入る、ハングしない | 端子に実際に何が出ているか |
| 2 | **ボード内loopback** — 同じboardの2 pinをジャンパで直結 | ジャンパ線1本 | 出力が入力として観測できる。実際の信号経路 | 波形の精度(周波数・duty・立ち上がり) |
| 3 | **デバッガによる状態確認** — probe-rsでレジスタ/RAMを読む | 配線ゼロ(既に繋がっている) | 周辺レジスタが期待値になっているか | 端子まで出ているか、タイミング |
| 4 | **ロジックアナライザ** | 高い。1台ずつ | 波形の精度そのもの | — |

方法3はこのrepositoryではまだ使っていませんが、**配線をまったく増やさずに済む唯一の方法**なので、
全boardにロジアナを繋げない以上、Tier B以下では2の代わりに使う価値があります。
`probe-rs read b32 <addr> <n>`で周辺レジスタが読めます
(例: USART1の`CTLR1` = `0x40013800 + 0x0C`)。

### 機能別の割り当て

| 機能 | 方法 | 必要な配線 | 現状 |
|---|---|---|---|
| Serial 送信 | 2 (probeのUART bridgeへ) | TX → probe RX | ✅ `smoke.py` / `serial_println` |
| Serial 受信 | 2 | RX ← probe TX | 🔧 `serial_echo`(実機未確認) |
| Serial ボーレート精度 | 4 | ロジアナをTXへ | ⬜ |
| `millis()` / `micros()` | 1(hostの経過時間と突き合わせ) | 不要 | ✅ `core_api` |
| `digitalWrite` / `digitalRead` | 1(出力pinは入力経路にも入る) | 不要 | ✅ `core_api` |
| `digitalRead` の入力・プルアップ | 2(出力pin → 入力pin) | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `analogRead` | 1(範囲チェック)、2(既知電圧をGND/VDDから) | ジャンパ1本 | ✅ 範囲のみ |
| `analogWrite` (PWM) duty | 2(PWM出力 → 別pinで`pulseIn`) | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `analogWrite` (PWM) 周波数 | 4 | ロジアナ | ⬜ |
| `attachInterrupt` | 1(自分の出力エッジを拾う) | 不要 | ✅ `core_api` |
| `attachInterrupt` の他ポート | 2 | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `shiftOut` / `shiftIn` | 2(2 pinを直結) | ジャンパ1本 | 🔧 ハングしないことのみ |
| `pulseIn` | 2(PWM出力を測る) | ジャンパ1本 | 🔧 timeoutのみ |
| ヒープ (`String` / `malloc`) | 1 | 不要 | ✅ `heap_string` |
| `printf` / stdio | 1 | TXのみ | ✅ `stdio_printf` |
| SPI | 2(MOSI → MISO 直結でloopback) | ジャンパ1本 | ⬜ 未実装 |
| SPI mode / clock | 4 | ロジアナ | ⬜ 未実装 |
| Wire (I2C) | 2(同一board上のslave役 or EEPROM) | 外部device | ⬜ 未実装 |
| USB-PD (X035) | 4 + 実negotiation | 専用治具 | ⬜ 未実装 |
| クロック設定 | 3(RCCレジスタ読み出し)、1(`millis()`の歩度) | 不要 | 🔧 間接的 |
| 割り込み優先度 / PFIC | 3 | 不要 | ⬜ |

凡例: ✅ 実装済み / 🔧 部分的 / ⬜ 未着手

### なぜ全boardにロジアナを繋げないか

ロジアナは1台をboard間で差し替えるしかなく、差し替えのたびに人が要ります。
したがって**方法4はTier Aの2 boardに限定**し、Tier B以下は方法1〜3で見ます。
波形の精度はboardではなく**周辺回路のIP**で決まるため、
同じIPを持つfamilyの代表1台で測れば十分という判断です。

---

## WCH-LinkEを1台しか繋げない制約

この環境では複数のWCH-LinkEを同時に使えないことが分かっています(USBIPが2台目を拒否)。
実機テストは逐次になります。選択肢は次の4つです。

| 案 | 内容 | 評価 |
|---|---|---|
| a | 人が差し替える(現状) | Tier Aだけなら現実的。CIには乗らない |
| b | 別ホスト(Raspberry Pi等)をrunnerにしてboard farmを置く | 本命。USBIPの制約を回避でき、self-hosted runnerにできる |
| c | セルフホストGitHub Actions runner | bの延長。まずbが要る |
| d | Tier Aを常時、Tier Bはrelease前に差し替え | 追加投資なしで今すぐできる |

**当面はd**とし、bを別途準備します。
dの前提として、**どのboardが繋がっているかをスクリプトが自分で判定できる**必要があるので、
`tests/manual/chip_info.py`がprobeとchipを問い合わせてFQBNとSerial pinまで出します。
`smoke.py`も`--board`と実チップが食い違えばフラッシュ前に落とします。

---

## テストカバレッジ一覧

| 機能 | 自動 | 手動 | 未カバー |
|------|------|------|---------|
| 全part numberのcompile | ✅ `compile`(122 part number、3 OS) | | |
| ELFサイズ回帰 | ✅ `compile/check_sizes.py` | | ⬜ RAM上限に対する警告 |
| crt0 / vector tableの正しさ | ✅ `startup`(EVT startupとのELF等価性、14 variant) | | |
| 生成物の同期 | ✅ `generated-sync`(device-dataから再生成してdiff) | | |
| vendored ArduinoCore-API の同期 | ✅ `api-sync` | | |
| Board Manager install → compile | ✅ `install-test`(3 OS、上書きなし) | | ⬜ upgrade経路、`libraries/`解決 |
| probe-rsのinstallと起動 | ✅ `install-test` | | |
| Serial 送信 | ✅ `serial_println`(V003 / V203 / X035 / L103 実機PASS) | ✅ `smoke.py` | ⬜ V103 / V307 実機 |
| Serial 受信 | 🔧 `serial_echo`(sketchのみ。実機未確認) | | ⬜ 実機、フロー制御、エラーフラグ |
| ヒープ(`String`/`malloc`/`free`/OOM) | ✅ `heap_string`(X035実機PASS) | | ⬜ 断片化、`realloc` |
| ビルドmenu(`pnum` / `printf`) | ✅ `compile_all.sh`(pnum)、実機(printf) | | ⬜ 全menu組み合わせのcompile |
| `printf` / stdio | ✅ `heap_string`(X035実機PASS) | | ⬜ float書式、`nano.specs`未適用でsketchが約40 KB膨らむ |
| 時間 (`millis`/`micros`/`delay`) | ✅ `core_api` | | ⬜ 長時間のオーバーフロー、歩度精度 |
| GPIO | ✅ `core_api`(出力の読み戻し) | | ⬜ 入力プルアップ、他ポートへのloopback |
| ADC | ✅ `core_api`(範囲) | | ⬜ 既知電圧での確度、分解能設定、複数ch |
| PWM | 🔧 `core_api`(ハングしないこと) | | ⬜ duty、周波数、pin ↔ timer対応 |
| 外部割り込み | ✅ `core_api`(自エッジ) | | ⬜ 他ポート、CHANGE/LOW、優先度 |
| `shiftOut` / `pulseIn` | 🔧 `core_api`(ハングしないこと) | | ⬜ 実波形 |
| 乱数 / 数学 | ✅ `core_api` | | |
| USART route自動判定 | | ✅ `uart_scan.py` | |
| チップ / probe判定 | | ✅ `chip_info.py` | |
| SPI | | | ⬜ **未実装** |
| Wire (I2C) | | | ⬜ **未実装** |
| USB (X035 PD / V307 HS) | | | ⬜ **未実装** |
| 低消費電力モード | | | ⬜ **未実装** |
| SDI printf(WCH-Link経由のdebug出力) | | | ⬜ **未実装**(別Serialクラスとして検討) |

---

## 実行方法

```sh
cd tests
uv sync
```

`arduino-cli`がPATHに必要です。

### 自動テスト(実機なし)

```sh
# 全part numberのcompile + サイズ回帰
CH32_GCC_BIN=<xpack>/bin tests/compile/test_compile.sh /tmp/w3

# Board Manager経由のclean install → 上書きなしcompile
CH32_XPACK_ARCHIVE=<archive> tools/index/test_install.sh /tmp/w5

# sketchテストをbuildだけで回す(CIが回す形)
cd tests && uv run pytest sketches --profile ch32x035 --run-mode build
```

### 自動テスト(実機あり)

```sh
uv run tests/manual/chip_info.py            # まず何が繋がっているか確認
cd tests && uv run --env-file .env pytest sketches --profile ch32x035 --port /dev/ttyACM4
```

### 手動テスト

```sh
uv run tests/manual/smoke.py                 # 繋がっているboardで受け入れsketch
uv run tests/manual/smoke.py --sketch all    # boardを差し替えたら
uv run tests/manual/uart_scan.py             # 配線が不明なとき
cd tests && uv run --env-file .env pytest manual/<case>/<case>.py -v -s
```

`--board`は省略できます。probe-rsが型番を読んで`boards.txt`から逆引きするので、
**焼く対象と焼く相手が食い違うことが原理的に起きません**。
明示すると検出結果に対する主張になり、食い違えば止まります。

作業台固有の値(手動testが使うpad等)は`tests/.env`で上書きします
([`.env.example`](.env.example)が説明)。

手動テストは常に`-s`を付けます。オペレータへの指示が端末に出ます。

---

## 関連文書

- [テスト戦略](../docs/test-strategy.ja.md) — レイヤー分けとsource of truthの考え方
- [環境整備計画](../docs/infrastructure.ja.md) — W-1〜W-7のworkstream
- [tests/README.ja.md](README.ja.md) — profileと開発中platformの関係、既知のarduino-cliの不具合
- [tests/manual/README.ja.md](manual/README.ja.md) — 作業台の配線とboardごとのSerial pin
