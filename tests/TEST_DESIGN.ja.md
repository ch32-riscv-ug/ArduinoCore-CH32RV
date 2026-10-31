# テスト構成の再設計案（2026-10-28）

[10-24 の検討](test-plan-review-2026-10-24.ja.md)を引き継ぎ、次の条件で組み直した案です。採否はまだ決めていません。

- OEP プローブを基本にする。全ファミリー（12）に、OEP でつないだボードを 1 台ずつ用意する予定。
- WCH-LinkE も複数台を用意する予定。
- 基本機能の確認と、特殊な構成ごとの確認を分ける。
- `--profiles` / `--combo` を使い、なるべく 1 回の実行で済ませる。
- 接続先は環境変数で選ぶ。OEP プローブなら、配線はプローブの設定から取る。

## 0. 実機で確かめる範囲の決め方

全部の組み合わせを実機で確かめることはできない（ボード ID 28、型番 105、CPU Clock の選択肢 84。opt 3 × Floating Point（rtlib）17 を掛けると 1 つの sketch で 4505 通り（2026-10-29）。Clock Source（HSI / HSE）、route とコントローラーで更に増える）。層に分ける。

1. **組み合わせはビルドで確かめる**（CI）。例の全プロファイル、各系列の CPU Clock の全項目など。容量のはみ出しとコンパイルの失敗はここで捕まえる。
2. **ロジックはホストの単体試験**（tests/unit）。生成器、ring buffer、deliverEvents、時刻、PD のフレームなど。
3. **表のデータは突き合わせ**。端子・route・クロック・DMA の表は device-data から生成し、データシートや EVT のヘッダとの照合を device-data の CI で確かめる。実機は使わない。
4. **実機はコードの分かれ道を 1 回ずつ通す**。組み合わせではなく分岐を数える。
   - 周辺の IP の世代ごとに 1 回（USBFS の 3 種類、USBHS の第 1 / 第 2 世代、SS、Ethernet の 10M と 10/100M など）。
   - 生成されたクロックの手順の型ごとに 1 回（PLL の型、H41x の SYSPLL、V407 の CFGR2、X315 の手順を書き出す型など）。
   - 端の値を 1 回（最低と最高のクロック、RAM の最も小さい系列）。
   - 効き合うと分かっているものだけ組で試す（USB × クロック、タイマーの取り合い × PWM / tone / Servo など）。
5. **確かめた範囲を記録する**。「実機で確認」と「ビルドのみ」を周辺機能表に書き分け、確かめていない組み合わせを確認済みのように見せない。

常設ベンチ（BENCH.ja.md）の選び方（ファミリーごとに機能の一番多い型番、型番で分かれるコードには境界の型番）は、4 に沿っている。

## 1. プラグインの最新版（2026-10-28 に確認）

| パッケージ | 固定版 | 最新版 | 扱い |
|---|---|---|---|
| pytest-embedded | 2.9.3 | 2.9.3 | 最新 |
| pytest-embedded-arduino-cli | 1.8.0 → **1.10.0** | 1.10.0 | 上げた（2d338937）。今の runner のまま x035c8 の runtime / usb-peer（LinkE）と l103-rp2350 の runtime（OEP）が通る |
| pytest-embedded-arduino-cli-ch32rv | 0.0.6 | 0.0.6 | 最新 |
| oep-client-python | 0.0.39 | 0.0.39 | 最新（プローブも 0.0.39） |
| pytest-embedded-wireskein | 未使用 | 0.0.4 | OEP の取り込みで波形を照合するときの候補（6 章） |

ガイド（README・TESTING_ADVANCED）で確かめた、この設計が頼る機能と規則:

- **`--profiles=all|P,...`（1.9.0）**: 各 sketch の profile を item に展開する。item ID・DUT・ログ・artifact は構成ごとに分かれる。
- **`--combo`（1.10.0）**: 列挙した組だけを実行する。`primary=P` だけの組も書ける（ソースで確認）。`tests/pyproject.toml` の `arduino_combos` に名前付きで置き、`--combo all` で全部を選ぶ。
  - sketch に無い profile を含む組は、その sketch には item を作らない。どの組も当てはまらないテストは、`combo=none` の skip が 1 件残る。
  - `--combo` は `--profile(s)` / `--peer-profile(s)` と併用できない。
- **port の解決**: profile ごとに `TEST_SERIAL_PORT_<PROFILE>`。PROFILE は大文字にし、`-` を `_` にした名前。peer は `TEST_SERIAL_PORT_PEER_<NAME>_<PROFILE>`。
  - 複数の候補がある役割では、共通の `--port` / `TEST_SERIAL_PORT` は使えない。
  - **port が無い構成は、compile の前に skip** する（設備による skip）。接続・build・upload の失敗は error。
- **`arduino_skip_if` / `pytest_arduino_cli_skip_reason(item, combination)`**: 収集時に適用条件で skip する。判定は設定だけを読み、機材に触れない契約。
- **`--run-mode=build`**: port は要らない。CI で全構成をビルドだけするのに使える。
- **`arduino_reset_dirs`（1.10.0）**: テストの前に output 系のディレクトリを消す。
- 実機の組合せ実行は逐次。xdist は使わない。

## 2. 全体の形

```text
tests/
  pyproject.toml        arduino_combos（生成）、testpaths は unit のまま
  profiles.toml         profile の正本（3 章）。手で書くのはここだけ
  .env.example          接続先の例（4 章）。.env はコミットしない
  unit/                 ホストだけの試験（今のまま）
  hw/
    conftest.py         作業ツリーの core の参照、OEP の配線の preflight と skip の判定
    basic/<契約>/       基本機能。sketch.yaml（生成）、.ino、test_*.py
    special/<話題>/     特殊な構成（clock の端、USB の役割、ETH、PD など）
    peer/<話題>/        相手の要る試験。peer_<名前>/ を同居させる
    ecosystem/<経路>/   書き込み・起動・更新の流れ（LinkE、ISP、DFU、製品の HID）
```

- 試験の sketch と test は同じディレクトリに置く（プラグインの標準の形。今の staging 型はやめる）。
- `sketch.yaml` は `profiles.toml` と各 sketch の `/* requires: ... */` から生成し、コミットする（例の profile と同じ方式。CI で生成物の一致を確かめる）。
- `arduino_combos` も `profiles.toml` から生成して、`pyproject.toml` の決まった区画に書く。

## 3. profile の命名と一覧

### 命名

`<経路>_<ボード>[_<構成>]`。sketch.yaml には小文字で書き、環境変数では大文字になる（`oep_ch32x035` → `TEST_SERIAL_PORT_OEP_CH32X035`）。

| 部分 | 値 | 意味 |
|---|---|---|
| 経路 | `oep` | OEP プローブで書き込み、コンソールは DMSEQ か OEP の fixture UART。周辺の刺激と観測もする（主軸） |
| | `linke` | WCH-LinkE で書き込み、コンソールは LinkE の UART か SDI。書き込み・起動・更新の流れを見る |
| | `isp` / `dfu` / `hid` | チップの USB ISP、USB DFU、製品の HID ブートローダー |
| ボード | `ch32x035`、`uiapduino_v003_v14` など | boards.txt のボード ID を小文字にしたもの。汎用ボードでは系列名になる。pnum はそのボードの正確な型番に固定する |
| 構成 | 下表の語彙だけ | 既定のメニューから変えたもの。無ければ既定（基本） |

構成の語彙（増やすときはこの表に足す）:

| 語 | 例 | 変えるもの |
|---|---|---|
| `clk<MHz>` | `clk8`、`clk240` | CPU Clock |
| `osltosr` / `float` | `oep_ch32v003_float` | opt / rtlib のメニュー |
| `usbdev` / `usbhost` / `usbhs` / `usbss` | `oep_ch32x035_usbhost` | USB の役割・コントローラー |
| `eth` / `pd` | `oep_ch32v307_eth` | ネットワーク・PD の設備が要る構成 |
| 型番・基板 | `oep_ch32x035_f8weact` | 汎用のボード ID のまま使う、同じ系列の 2 台目以降の基板（WeAct の X035F8U6 など） |
| `rel` | `linke_ch32v307_rel` | 公開版の package を使う（作業ツリーではない。7 章） |

机や個体の名前は profile に入れない。どの個体につなぐかは `.env` で決める。

### 常設のベンチ

- ファミリーごとの 12 台に加え、**ボードとして対応しているもの**（boards.txt の製品ボード、例を持つ基板）も常設にする。
- 製品ボードは、boards.txt のボード ID を profile の名前に使う（`oep_uiapduino_v003_v14`）。汎用のボード ID で使う基板（WeAct の X035 など）は、系列名の後ろに基板の語を付ける（`oep_ch32x035_f8weact`）。
- 製品ボードは、ファミリー用のボードと P4 を共有してよい（5 章「P4 の共有」）。

### 基本の一覧（ファミリーごとに 1 つ）

| ファミリー | profile | 想定のボード（今の手持ち） |
|---|---|---|
| V003 | `oep_ch32v003` | CH32V003F4U6 / F4P6（あり） |
| V00x | `oep_ch32v006` | CH32V006（あり） |
| V103 | `oep_ch32v103` | CH32V103R8T6（あり） |
| V20x | `oep_ch32v203` | CH32V203C8T6（あり） |
| V205 | `oep_ch32v205` | CH32V205RCT6（あり。LinkE の接続で s0 が壊れる件は OEP では未確認） |
| V30x | `oep_ch32v307` | CH32V307VCT6（あり） |
| V4x7 | `oep_ch32v407` | CH32V407WEU6（P4 の後ろにあり） |
| L103 | `oep_ch32l103` | CH32L103C8T6（RP2350 の後ろにあり） |
| M030 | `oep_ch32m030` | CH32M030G8R7（ch32rv セッションの手元） |
| X035 | `oep_ch32x035` | CH32X035C8T6（あり） |
| X3x5 | `oep_ch32x315` | CH32X315MCU6（あり） |
| H41x | `oep_ch32h417` | CH32H417QEU6（あり。今は LinkE） |

### 特殊な構成（最初の候補）

| 話題 | profile | 要るもの |
|---|---|---|
| clock の端 | `oep_ch32v205_clk8`、`oep_ch32v407_clk20`、`oep_ch32x315_clk240`、`oep_ch32m030_clk8`、`oep_ch32l103_clk96` | 同じボード。L103 の 72 / 96 MHz は OEP から接続できない件を先に片付ける |
| 小さい RAM と浮動小数点 | `oep_ch32v003_float` | 同じボード |
| USB device / host | `oep_ch32x035_usbdev` + peer `esp32s3_usbhost`、`oep_ch32x035_usbhost` + peer `esp32s3_usbdev` | ESP32-S3 の peer（今のベンチにあり） |
| USB HS / SS | `oep_ch32v307_usbhs`、`oep_ch32h417_usbss` | PC のホスト |
| Ethernet | `oep_ch32v307_eth`、`oep_ch32v407_eth`、`oep_ch32h417_eth` | DHCP のある LAN と、相手になる PC |
| PD | `oep_ch32x035_pd` | PD の電源 |

### エコシステム（LinkE などの経路）

LinkE につないだボードごとに `linke_<系列>`。ISP / DFU / HID は経路を持つボードだけ（`isp_ch32x035`、`dfu_ch32h417`、`hid_uiapduino_v003`）。公開版を使う確認は `_rel` を付けた profile で別に持つ。

## 4. 1 回で実行する

`arduino_combos` に、すべての構成を 1 行ずつ並べる（`profiles.toml` から生成）。

```toml
arduino_combos = [
  "oep_ch32x035: primary=oep_ch32x035",
  "oep_ch32v307: primary=oep_ch32v307",
  # ... 基本・特殊・エコシステムの全 profile
  "x035_usbdev: primary=oep_ch32x035_usbdev,host=esp32s3_usbhost",
]
```

```sh
uv run --env-file .env pytest hw --combo all            # 全部（.env にある接続先だけが動き、ほかは設備で skip）
uv run --env-file .env pytest hw/basic --combo all      # 基本だけ
uv run --env-file .env pytest hw --combo oep_ch32x035   # 1 ボードだけ
uv run pytest hw --combo all --run-mode=build           # CI: 実機なしで全構成をビルド
uv run pytest hw --combo all --collect-only -q          # 何が動くかと、適用条件の skip を見る
```

- sketch.yaml に無い profile の組は item を作らないので、`--combo all` は各 sketch に当てはまる構成だけを展開する。
- `.env` に無い接続先は compile の前に skip する。手元の機材だけで回る。
- 必須の構成が skip されていないかは、結果（JUnit・summary.json）で確かめる。全部 skip でも終了値は 0 になるため。

## 5. 接続先の選択と、OEP の配線の取り方

### 環境変数（.env）

```dotenv
TEST_SERIAL_PORT_OEP_CH32X035=oep://<プローブ>/x035
TEST_SERIAL_PORT_OEP_CH32V407=oep://30eda0ea068b/v407
TEST_SERIAL_PORT_LINKE_CH32V307=wchlink://<LinkE の serial>
TEST_SERIAL_PORT_PEER_HOST_ESP32S3_USBHOST=/dev/serial/by-id/<...>
```

渡すのは接続先だけです。ピン・チャネル・機能の有無を表す変数は作りません。

### 配線はプローブのラベルから取る

- ラベルの規約は `<口>.<線>`。口は接続先 `oep://<プローブ>/<口>` の `<口>`。
- 線の名前:
  - デバッグとリセット: `swio` / `swdio` / `swclk` / `nrst`（今ある名前のまま）。
  - コンソール: `rx` / `tx`（OEP 側から見た向き。ch32rv プラグインの `ch32_uart` が読む名前）。
  - それ以外の配線: DUT の端子名を小文字で書く（`pa10`）。機能や役割は名前に入れない。
- 1 本の線にラベルは 1 つだけ付けられるので、`rx` / `tx` に使った線には端子名を付けない。
- 今のベンチのラベルは `v003.swio` / `v003.nrst` / `v003.power_hi` / `v407.swio` / `l103.swdio` / `l103.swclk` / `l103.nrst` で、この規約に合う。UART と周辺の配線は、まだ登録されていない。

### 判定の流れ

1. **preflight**: セッションの始め（`pytest_sessionstart`）に、選ばれた構成のうち `oep://` の接続先を持つものについて、プローブのラベルを 1 回ずつ読む。ターゲットには接続しない。
2. **要求**: 各テストは、要る端子を marker で宣言する。端子を直接書く形（`oep_pads("pa10", "pa11")`）と、route から引く形（`oep_route("i2c", 1)`。variant の `pins_arduino.h` をファイルとして読み、端子に直す）の 2 つを用意する。
3. **判定**: `pytest_arduino_cli_skip_reason` が、構成の口のラベルと要求を照らし、足りなければ `missing wiring: x035.pa11` で skip する。build の前に決まる。
4. **使う**: 試験の本文では、`oep_host` からラベルで線を引いて刺激・観測する（plan は試験の間だけ）。

区別する状態:

- ラベルが読めて、要る線が無い: skip（その試験だけ。ほかは続ける）。
- プローブにつながらない・読めない・同じ端子が 2 本ある: 設定エラー。skip にしない。
- 配線はあるのに期待どおり動かない: FAIL。

ターゲットが正しい個体かは、プローブからの接続では確かめません（V205 の件のように、接続が動作を乱すことがあるため）。書き込んだ sketch が、READY の応答で系列・型番・build ID を返し、profile と照合します。

### P4 の共有

1 台の P4 に口を複数登録したときにだけ出る不具合があります（口の選び方、ラベルの引き方、前の口の試験が残した plan や状態、口を切り替えたときのセッションの受け渡し）。試験は順に回るので、確かめるのは「口を切り替えながら順に使う」ことです。専用のプローブだけで組むと、これを試す機会がありません。

| 組み方 | 利点 | 欠点 |
|---|---|---|
| ピンの少ないボード（V003、V00x、製品ボード）を 1 台の P4 で共有 | 口が複数の構成を常に使うので、上の不具合が普段の実行で出る。P4 の台数が減る | 配線の本数が P4 の端子数で頭打ち |
| ボードごとに専用の P4 | 配線を多くできる | 口が複数の構成を試さない。台数が増える |

**案**: 両方を混ぜます。ピンの少ないボードと製品ボードは、何台かを 1 台の P4 で共有します。周辺の配線を多くとりたいファミリー（V307、V407、H417 など）は専用にします。共有の P4 が常設に少なくとも 1 台あれば、口が複数の構成は普段の実行で必ず通ります。

試験の側が守ること:

- 試験は、自分の口のラベルだけを読む。ほかの口の線や、プローブ全体の設定を変えない。
- plan は試験の間だけ持ち、終わったら必ず解放する（ほかの口の試験が続くため）。
- 同じ P4 の複数の口を、1 つの試験の中で同時に使う構成（例: 2 台の DUT の間の通信）は、peer の試験として別に持つ。

**実装の場所**: preflight・marker・判定は ch32rv プラグインに入れるのが自然です（OEP と CH32 の両方を知っているため）。最初は `tests/hw/conftest.py` で試作し、形が決まったら ch32rv プラグインへ移す依頼をします。

## 6. 波形の照合（任意）

OEP の P4 は取り込み（ロジックアナライザ）ができます。PWM・UART・I2C・SPI の波形の期待は、pytest-embedded-wireskein の `ws_run` で記録し、照合できます。プローブを動かす fixture（取り込みの開始と読み出し）は、ボードの系列ごとのプラグインが用意する前提なので、ch32rv プラグインに `oep_capture` を足してもらう必要があります。基本機能のうち、波形でしかわからないもの（PWM の周期とデューティ、UART のボーレート）から使います。

## 7. 作業ツリーの core と公開版

- 試験の profile は、作業ツリーの core を使います。生成する sketch.yaml には `platforms:` を書かず、`hw/conftest.py` が `ARDUINO_DIRECTORIES_USER` を作業ツリーへのリンクを持つ sketchbook に向けます（`tools/index/build_examples.py` と同じ作り）。
  - プラグインのガイドは「platform の版を固定せよ」と勧めていますが、core を開発中の試験では、作業ツリーを試すことが目的なので、意図して外します。
- `_rel` の profile だけは、公開版（`platform` と `platform_index_url`）を固定します。公開した package の導入から書き込みまでを確かめるためです。

## 8. 排他と結果

- **試験は同時に走らせない。** pytest は 1 回の実行の中で試験を順に回し、試験の実行を同時に 2 つ起こすこともしない。この前提なので、試験の側でロックは取らない（`bench.lock` も使わない）。
  - 機器の排他は pytest の機器ロック（`--device-lock required`）に任せる。WCH-Link／LinkE は Link の UART の tty、OEP のプローブ（`oep://` の DUT を含む）はプローブ自身の lease で排他する。ベンチの `bench.lock` は OEP の道具だけが使うもので、このリポジトリの道具も手作業も取らない（2026-10-31、ベンチの決まり）。uhubctl にロックの決まりはない。
  - OEP のプローブはロックを 1 つ持ち、ロックを持つセッションだけが状態を変えられる（oep-core §6.1）。ch32rv はプローブごとのブローカーがこのセッションを持ち、ほかの ch32rv の処理はそれを共有する（2026-10-28、V407 の `monitor` 中に別プロセスの `read` が 0.1 秒で通った）。手作業で ch32rv を叩くときは、試験の実行と重ねないことを運用で守る。
  - lease（ch32rv プラグインの session は 60 秒）より長く黙る試験は、セッションが切れる。長い待ちのある試験は keepalive の要る作りにする。
- **結果**: プラグインの summary.json と JUnit を使います。今の `result.json`（ツールの版、commit、設定の hash）に当たる情報は、`hw/conftest.py` が `record_property` で各テストに付けます。
- **結果**: プラグインの summary.json と JUnit を使います。今の `result.json`（ツールの版、commit、設定の hash）に当たる情報は、`hw/conftest.py` が `record_property` で各テストに付けます。

## 9. 移行の順番

1. pytest-embedded-arduino-cli 1.10.0 に上げる（済み）。
2. `profiles.toml`、生成器、`.env.example` を作り、`hw/basic/runtime` と `hw/special/logic` を 2 ファミリー（x035、v307）で `--combo all` で回す。
3. OEP の配線の preflight と skip を `hw/conftest.py` で試作し、ラベルの無い構成・ある構成・あるのに応答しない構成の 3 つで、skip・実行・失敗に分かれることを確かめる。
4. 今の case（runtime、uart、gpio、power、usb*、eth、logic、pd、synckit）を 1 つずつ `hw/` に移す。全部移したら、`run_hardware.py` と `fixtures/bench.*.local.toml` を消す。
5. CI に `pytest hw --combo all --run-mode=build` を足す。

## 10. 決めていないこと・他リポジトリへの依頼

- **基本の sketch の粒度**: 契約ごとに sketch を分けると、ファミリーごとの書き込み回数が増えます（12 ファミリー × 8 契約 = 96 回、1 回 50 秒として約 80 分）。1 つの sketch に多くの試験関数を入れると速くなりますが、失敗の場所がわかりにくくなります。2 の試作で、両方の時間を測ってから決めます。
- **P4 を共有するか、ボードごとに専用にするか**（5 章「P4 の共有」）。
- **ch32rv プラグインへの依頼**:
  - OEP の配線の preflight・marker・skip の判定。
  - `oep_capture`（波形の取り込み）。
- **OEP のラベルの登録**: 各ボードの UART（`rx` / `tx`）と、周辺の配線（端子名）を、プローブの設定に登録する作業が要ります。配線を変えたら、プローブの設定を直します。正本はプローブの設定だけにします。
- **LinkE の UART**: LinkE の UART は、ボードによってはつながっていません（V205 / X315）。`linke_` の profile のコンソールは、つながっているボードは UART、つながっていないボードは SDI か DMSEQ を profile ごとに選びます。
