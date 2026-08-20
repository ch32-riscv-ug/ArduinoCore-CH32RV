# 実験0012: probe-rsによる書き込み経路の実体化

実施日: 2026-08-20
対象question: Q-040(programmer tool定義)、Q-041(probe選択)、Q-044/Q-045(独自tool要否)、[ADR-0008](../adr/0008-upload-strategy.ja.md)
実施環境: WSL2、arduino-cli 1.3.1、probe-rs 0.32.0、**CH32V203実機 + WCH-LinkE v2.12**

## 結果: `arduino-cli upload`から実機書き込みまで通った

```
arduino-cli upload --fqbn ch32-riscv-ug:ch32v:CH32V203:pnum=ANY --programmer wch-link
  -> probe-rs download --chip CH32V203C8T6 --binary-format elf --non-interactive --reset ...
  -> Finished in 3.14s
  -> シリアルに hello from ch32 / int=42 / hex=BEEF
```

**Q-044(独自uploader `ch32-upload`)とQ-045(独自programmer)の昇格条件は満たされない。**
既存toolだけで、Arduino標準の経路がそのまま成立する。

## 配布方法

| 項目 | 決定 |
|---|---|
| 入手元 | [probe-rs GitHub Releases](https://github.com/probe-rs/probe-rs/releases)の`probe-rs-tools-<target>`。**再ホストしない**([ADR-0002](../adr/0002-toolchain-distribution.ja.md)と同じ方針) |
| version | `0.32.0`固定。[`tools/index/tools_probe_rs.json`](../../tools/index/tools_probe_rs.json) |
| checksum | releaseの`sha256.sum`から転記。sizeはGitHub API |
| host | linux x64/arm64、macOS x64/arm64、Windows x64(`i686-mingw32`にも同じzipを割り当て) |

**Linux/macOSの配布形式は`.tar.xz`**。Arduinoのtoolは`.tar.bz2`/`.zip`が主流なので
扱えるか不明だったが、**arduino-cli 1.3.1はclean installで問題なく展開した**
(`tools/index/install_check.py`に`probe-rs --version`まで走らせるcheckを追加済み)。
archiveは単一rootフォルダ構成で、Arduinoのtool規約にそのまま合う。

## chip名の解決

`probe-rs download`は**曖昧なchip名を拒否する**。`chip info`は先頭一致で通るので
最初は`--chip CH32V203`で足りると誤解したが、実際には:

```
Error: The chip 'CH32V203' was not found in the database.
  Found multiple chips matching 'CH32V203', unable to select a single chip.
```

したがって**全menu entryに具体的な型番を生成する**。
[`tools/index/probe_rs_targets.csv`](../../tools/index/probe_rs_targets.csv)
(`probe-rs chip list`から抽出、127 target)を入力に、`generate.py`が
`<board>.menu.pnum.<pnum>.build.probe_rs_chip`を出力する。

- 型番が一致すればそのまま使う
- 無ければ**同seriesでprobe-rsが知っている最小flashの型番**へ落とす。
  flash algorithmはfamily単位であり、容量境界は`upload.maximum_size`が既に守っている
- `ANY`も同じ規則。`ANY`は元々seriesの最小容量を宣言しているので整合する

**117 entry中90にtargetがある。** 残る27は7 series(M030/M103/V205/V407/V467/X305/X315)で、
**`[compile only]`表示はこのcoverageから自動で決まる**ようにした
(以前は`SERIES_CONFIG`の手書きフラグ。CH32M103が漏れていたのが実際に見つかった)。

## probe選択

`probe-rs list`が`1a86:8010:FBC18F0680B0`形式で出す値をそのまま`--probe`に渡せる。
platform側は`upload.probe_args`で受ける:

```
arduino-cli upload --programmer wch-link \
  --upload-property upload.probe_args="--probe 1a86:8010:FBC18F0680B0"
```

flag全体を1プロパティにしているのは、**空のときにコマンドラインから消える必要がある**ため。
`--probe {upload.probe}`だと未指定時に空引数が残ってprobe-rsが落ちる。

## `upload.tool`が必要だった

`--programmer`を指定していても、arduino-cli 1.3.1は先に`upload.tool.default`を解決する:

```
Error during Upload: Property 'upload.tool.default' is undefined
```

そのため`upload.tool=probe_rs`も定義した。ただし**素の`arduino-cli upload`は
依然`A programmer is required to upload`で拒否される**。
serial bootloaderが無いので、これは正しい挙動である。

## `download --reset`

`probe-rs download`単体では**書き込み後にCPUがhaltしたまま**で、
別途`probe-rs reset`が要る。Arduinoのprogrammer patternは1コマンドしか持てないので、
これは問題になるところだった。`download --reset`が両方をやるので1行で収まった。

## 実機での確認手順

`tests/manual/smoke/smoke.py`を**minichlink直叩きから`arduino-cli upload --programmer`へ移した**。
これでsmoke testは「コードが動く」ではなく「出荷経路が動く」ことを見る。
副次的にminichlink依存が消え、**同梱minichlinkが知らないCH32L103も扱えるようになった**
(`Chip Type unknown [0e]`で書き込めなかった)。

## 未確認

- Windows / macOSでの実行(CIのcompile matrixは3 OSだが、書き込みは実機が要る)
- `[compile only]` 7 seriesの書き込み経路(wlink併用が候補)
- probe-rsのversion追随方針。現状は固定で、更新は手動
