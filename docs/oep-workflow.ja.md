# OEP を含む開発ワークフロー（最終の形）

文書基準日: 2026-09-29。状態: **決定**（2026-09-29 の議論で決めた最終の形。未決の点は §10 に列挙）。ここから実装を進め、
実機で確かめて直す。個々の仕様は各リポジトリの文書が正で、ここは分担と、リポジトリをまたぐ決定を置く。

## 0. 用語（英語で決め、日本語は訳）

| English | 日本語 | 意味 |
|---|---|---|
| **transport** | トランスポート | OEP のフレームを運べるもの。vendor bulk、HID、USB CDC、USB-Serial/JTAG、UART bridge、TCP |
| **serial port** | シリアルポート | transport のうち、OS からシリアルデバイスに見えるもの（USB CDC の interface、USB-Serial/JTAG、UART bridge）。probe が番号で宣言する。常に OEP を受け、それ以外のバイトは bind したストリームへ流す。vendor bulk と HID は transport だが serial port ではない |
| **stream** | ストリーム | probe の中の位置付きのバイト列。target console と fixture UART の受信。読んでも消えず、読み手が複数いてよい |
| **slot** | スロット | 登録された 1 つの「場所」: wire、pins、name、attach policy、console mechanism。IDE port はスロットごと |
| **connection** | 接続 | スロットへの生きている attach |
| **bind** | バインド | serial port → stream の対応（1 port に 1 stream） |
| **attach policy** | attach の方針 | host / at boot（+ 再試行の間隔） |
| **IDE port** | IDE のポート | Arduino の discovery が並べる 1 行。probe の serial port とは別の概念 |
| **broker** | ブローカー | probe ごとに 1 つ、誰の子でもない ch32rv のプロセス。probe のセッションを持ち、client（ch32rv の各コマンド、pytest）を束ねる。client が 0 になったら終わる |

wire（線。debug の線を駆動するインターフェース: `oep.wire.rvswd` / `oep.wire.swio`）、pins、console は仕様の語のまま。

## 1. 登場するもの

| もの | リポジトリ | 受け持つこと |
|---|---|---|
| ArduinoCore-CH32RV | [ch32-riscv-ug/ArduinoCore-CH32RV](https://github.com/ch32-riscv-ug/ArduinoCore-CH32RV)（このリポジトリ） | core、ボード定義、試験、この文書 |
| ch32rv | [ch32-riscv-ug/ch32rv](https://github.com/ch32-riscv-ug/ch32rv) | 同梱の書き込みツール。書き込み・デバッグ・モニタ・**discovery**・ブローカー |
| wch-protocols | [ch32-riscv-ug/wch-protocols](https://github.com/ch32-riscv-ug/wch-protocols) | 実測の台帳（読むだけ） |
| OEP の仕様 | [Open-Embedded-Probe/oep-spec](https://github.com/Open-Embedded-Probe/oep-spec) | probe と host の間のプロトコル |
| OEP の probe | [Open-Embedded-Probe/oep-probe-arduino](https://github.com/Open-Embedded-Probe/oep-probe-arduino) | ESP32-P4、classic ESP32、RP2350 の probe の実装 |
| OEP の client | [Open-Embedded-Probe/oep-client-python](https://github.com/Open-Embedded-Probe/oep-client-python) | Python の host 側（純粋なモジュール） |
| WireSkein | [Open-Embedded-Probe/wireskein](https://github.com/Open-Embedded-Probe/wireskein) | ロジックの記録の解析と照合（純粋なモジュール） |
| pytest-embedded-arduino-cli | [tanakamasayuki/pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) | pytest から arduino-cli で upload / monitor |
| pytest-embedded-arduino-cli-ch32rv | （新しいリポジトリ） | ch32rv の path、ブローカーの口、`oep_host` |
| pytest-embedded-wireskein | （新しいリポジトリ） | test ごとの記録と照合 |

## 2. 使う場面

| 場面 | 書き込み | 実行中の出力を見る | 波形 |
|---|---|---|---|
| 利用者、WCH-LinkE | ch32rv（LinkE の CDC か `wchlink://` を選ぶ） | ch32rv の monitor（uart / sdi / dmdata / dmseq / rtt） | — |
| 利用者、OEP の probe | ch32rv（`oep://<probe>/<slot>` か、probe の serial port を選ぶ） | ch32rv の monitor（同じ IDE port） | — |
| 利用者、UIAPduino（USB だけ） | ch32rv（HID のブートローダー。手動の pin reset） | 無し（LinkE / OEP の側） | — |
| 開発、ベンチ（pytest） | arduino-cli → ch32rv | arduino-cli monitor → ch32rv | OEP の client → WireSkein |

## 3. 書き込みの経路

### 3.1 対応する経路

| 経路 | 家系 | 入り方 | 状態 |
|---|---|---|---|
| **A. WCH-Link / LinkE**（RVSWD、V003 / V00x は SWIO） | 全部 | 常時 | 現状維持（ch32rv `flash`） |
| **B. OEP の probe**（RVSWD / SWIO） | 全部 | 常時 | **ch32rv で対応する**。pytest も arduino-cli 経由で書く。client の `ch32_flash` は ch32rv の OEP の書き込みが入った時点で消す |
| **E. UIAPduino の HID ブートローダー**（`1209:b803`） | ブートローダーを入れた V003 の板 | **手動の pin reset**（probe があれば A / B を使う） | 現状維持（ch32rv `boot hid flash`）+ discovery |
| **C. factory ISP、USB**（`4348:55e0`） | BOOT0 / BOOT1 ピンの家系（V103 / V20x / V30x / V31x / L103 / V407）。X035 / X315 はソフトからしか入れない | BOOT ピン、または app の協力 | **設計は決めた。最初の β には入れず、後から足す**（§3.5） |
| D. factory ISP、UART | V003 / V00x（app の協力が要る）ほか | — | **対応しない**。変換チップを足すなら、その ESP32 を OEP の probe にする |
| F. 独自のブートローダー（DFU / UF2 / UART） | そのブートローダーを入れた板（今は無い） | — | **対応しない**。DFU の板が現れたら recipe を足すだけ（discovery は arduino-cli の組み込み） |

ARM の SWD（`oep.wire.swd`、LinkE の DAP モード）は、この core の target には出てこない。

### 3.2 IDE port を選ぶだけで書く

Uno と同じく、プログラマの選択は要らない。板は `upload.protocol` を持ち（無いと arduino-cli がプログラマを要求する）、
1 つの書き込みツール（ch32rv）が IDE port の protocol と properties から経路を決める。`upload.tool.<protocol>` はすべて ch32rv。
プログラマのメニューは、書き込み装置が複数つながっているときの上書きに残す。`--upload-property` / `--build-property` は使わない
（sketch.yaml の `default_fqbn` / `default_port` / `default_programmer` / profile と、platform / boards の定義だけで動く）。

| 選んだ IDE port | 見分け方 | 書き込み |
|---|---|---|
| LinkE の CDC（serial） | ch32rv が `probe list` の `ports` から Link を引く（`--probe port:<path>`。シリアル番号が無くても topology で辿れる） | LinkE |
| `wchlink://<serial>` | discovery（実装済み） | LinkE |
| `oep://<probe>/<slot>` | discovery（専用 PID の probe だけ、§3.3） | そのスロット |
| OEP の probe の serial port（専用 PID の無い probe） | ch32rv が開いて describe が返る | 板の家系に合うスロット（§3.4） |
| `hid://<topology>`（UIAPduino） | discovery | HID ブートローダー |
| ポート無し | `upload.tool.default` | ch32rv が USB を走査し、候補が 1 つならそこ。複数なら候補を並べて断る |

### 3.3 discovery

discovery をするツールは **ch32rv**（`ch32rv arduino discovery`。pluggable discovery のプロトコルを stdio の JSON で話す）。
platform.txt の `pluggable_discovery.ch32rv.pattern` で登録する（1 つでも自前を書くと組み込みの serial / mdns も
`pluggable_discovery.required.N` で明示が要る）。Board Manager で core を入れると tool の依存として ch32rv が入るので、利用者に
別のインストールは要らない。組み込みの discovery（serial、mdns、dfu）はそのまま並走し、普通の serial port は組み込みが出す。

**ch32rv の discovery は普通の serial port を開かない**（開くと DTR でリセットされる板があり、OEP でない target に文字が届き、他の
道具の排他 open と競合する）。host 側の学習（一度開いたら以後出す）は持たない。

| 出すもの | 条件 | 中身 |
|---|---|---|
| `wchlink://<serial>` | LinkE（`1a86:8010` / `8012`） | USB の記述子だけ。target は引かない（LinkE の attach は target のクロックを組み替える） |
| `oep://<probe>/<slot>` | **専用 PID の USB で列挙する probe だけ**（P4 の HS の口、RP2350。OEP は pid.codes で PID を取る） | ロック無しの describe / config で登録（スロット）と状態を読む。**HID の口があれば HID で読む**（他の道具が CDC / vendor を握っていても読める。競合しない）。無ければ CDC / vendor を短く開き、busy なら前回の写し |
| `hid://<topology>` | UIAPduino のブートローダー（`1209:b803` / `b003`） | boards.txt の `upload_port.0.vid/pid` で板名を出す |
| `isp://<topology>`（後で） | ISP で待つ device（`4348:55e0`） | Identify で chip を読み、LinkE の IAP は除く。properties の `chip` で boards.txt の `upload_port.N.protocol=isp` / `.chip=…` と照合して板名を出す |

専用 PID の無い OEP の probe（変換チップ越しの無印 ESP32、USJ だけの P4）は discovery に出さず、利用者が serial port を選ぶ。

OEP の probe は、取得したプロジェクトの USB ID **1209:4F45** で見分ける（oep-spec core §3.3）。その device の中の口は interface の
種類で決まる（CDC はシリアルの口、vendor class の bulk は vendor bulk、vendor 定義の HID は HID）。interface や device の文字列
（iProduct）は表示のためのもので、見分けには使わない。利用者が `oep://<unit_id>` で名指ししたときは、USB の serial number が同じ
device を開いて describe の unit_id で確かめる。以前の仮の ID（303a:0002）と名前（iProduct が `OEP` で始まる）での判定は、
2026-10-06 の一斉切り替えで互換を残さずにやめた。

`oep://<probe>/<slot>` の `<slot>` はスロットの name（1〜32 byte、`a-z 0-9 - _`、probe の中で重ならない）。address は
sketch.yaml や pytest の `--port` に書かれるので、読めることを優先した。name を変えると address も変わる。

### 3.4 スロットの選び方（専用 PID の無い probe の serial port を選んだとき）

書き込みもモニタも同じ規則。利用者が IDE で選んだ板の家系（recipe の `--chip {build.ch32rv_chip}`。monitor には boards.txt の
`monitor_port.serial.chip=<家系>` を CONFIGURE で渡す）に合うスロットが **1 つならそこ**。接続済みのスロットの chip は probe が
持つ状態（§5.3）から、未接続のスロットは止めない attach で読む（利用者の操作なので線を駆動してよい）。**0 か 2 つ以上なら止めて**、
スロットの一覧と理由を出す。動きは挿さっているチップの組で決まり、登録の数では変わらない。ch32rv の `--chip` の fail-closed と同じ。
専用 PID の probe では、raw の serial port を選んだ upload は常に断る（`oep://…` を選ぶ）。

### 3.5 C（factory ISP、USB）を後から足すときの形

1 段目（BOOT ピンの家系）: ch32rv に `isp flash`、discovery に `isp://`、core に `upload.tool.isp` の recipe と boards.txt の照合。
monitor は無い。2 段目（X035 / X315）: core に USB CDC（TinyUSB の結線）と touch1200 → `FLASH_STATR.BOOT_MODE` + software reset
の入口を作ってから。全部足すだけで既存の経路に触れない。

## 4. probe の serial port の原則

### 4.1 常に OEP を受け、それ以外はコンソールへ

1. シリアルに見える transport（USB CDC、USB-Serial/JTAG、UART bridge）は、いつでも OEP のフレームを受ける。フレーム以外の
   バイトは bind したストリームへ流す（bind が無ければ捨てる）。
2. vendor bulk / HID を持つ probe では host はそちらで OEP を話す（core §3.3 の順）。CDC は実際にはコンソールだけが流れるが、
   OEP を受けなくなるわけではない（Web Serial や権限の無い環境からは CDC で話せる）。
3. どの transport から来た要求も、セッションとロックは 1 つ（core §3.3）。
4. 起動モード（setup / debugger）は持たない。設定は同じ口で行い、bind はすぐ効く。再起動も、setup へ戻る手段も要らない。

### 4.2 フレームの形と共用の規則

- シリアルに見える transport はすべて **COBS + CRC-16/CCITT-FALSE、0x00 区切り**（UART と同じ）。`length(u16) message` は vendor bulk
  と HID（と TCP）だけ。COBS の膨らみは 254 byte に 1 byte、CDC は制御にしか使わないので差は出ない。
- PC → probe: 0x00 が来たら次の 0x00 までためて解き、CRC が合えば OEP。合わなければ（200 ms 途切れても）ためた分をコンソールへ。
  0x00 以外はすぐコンソールへ。
- probe → PC: 応答と push は `0x00 <COBS> 0x00`。**serial port ごとに送信のキューを 1 つ**（単位はフレーム丸ごと、または
  コンソールの 64 byte 程度の塊。1 つの書き手が割らずに出す。フレームは塊より先に出してよく、塊どうしの順は保つ。塊は位置付きの
  ストリームから読むので捨てなくてよい）。今の Endpoint は `Serial` に直接書き、`HardwareSerial::write` は呼び出し単位でしか排他
  しないので、この形にする。
- **生の転送を止めるのは、ロックを持つセッションの要求が来ている serial port だけ。** 別の transport にセッションがあるとき、その
  serial port の生の転送は止めない。セッション中に PC から来たフレーム外のバイトは捨てる。
- セッションが終わったら（`end`、lease の期限切れ）、**止めた位置から**生の転送を再開する（あふれていれば一番古いバイトから）。
- client: フレームの外のバイトは雑音として捨て、応答の欠落は時間切れだけで判断する。

### 4.3 排他とロックの奪い方

- **OEP の道具はシリアルを必ず排他で開く**（Linux / macOS は `TIOCEXCL`、Windows は元から排他）。排他でないと応答のバイトが別の
  プロセスに渡り、経路そのものが成り立たない。`arduino-cli monitor` は既に TIOCEXCL を使っている。
- **transport がシリアル 1 本だけの probe**は、排他で開けた時点で前の持ち主は死んでいるので、ロックはその場で force で奪ってよい。
- **transport が複数の probe**は `lock_state` で持ち主と lease の残りを読み、残りだけ待ち（上限は数秒）、持ち主が更新し続けているなら
  名指しでエラー。force は利用者が明示したときだけ。対話的な道具は lease を短く（2〜3 秒）、pytest は長く（10 秒、更新あり）。
- 「1 本だけか」は **fn 0 の describe の経路の一覧**（新しい tag。種類の並び: UART bridge / USB CDC / USB-Serial/JTAG / vendor bulk /
  HID / TCP）で判断する。USB の列挙は discovery がポートを probe ごとにまとめるためだけに使う。

### 4.4 probe の再起動の抑止

| 口 | 止め方 |
|---|---|
| P4 の USB-Serial/JTAG | `USB_DEVICE_CHIP_RST_REG` の `USB_UART_CHIP_RST_DIS`（bit2）。arduino-esp32 3.3.12 の HWCDC に API は無い |
| TinyUSB の CDC（`USBCDC`） | `enableReboot(false)`（DTR / RTS の並びと 1200 baud の touch を止める） |
| EspUsbDevice の CDC（P4 の HS） | 元から持たない |
| classic ESP32 の UART bridge | 回路なので firmware では止められない。DTR と RTS を両方 on で開けば再起動しない（`arduino-cli monitor` の既定） |

### 4.5 リンクの速さ

UART bridge の probe は **115200 固定**。USB CDC / USJ では baud は数字が渡るだけで問題にならない。設定で変えられるようにすると
忘れたときに入れなくなり、自動 baud 検出は生のバイトと混ざる口では危うい。dmseq と書き込みには足りる。

## 5. OEP の probe の設定

### 5.1 流れ

1. ブラウザの設定ページ（OEP 側のリポジトリ）で、probe に合う transport（CDC は Web Serial、HID は WebHID、vendor は WebUSB）を
   開く。どれも同じ OEP（describe、session、probe.config）。
2. `scan` で target を探し、見つかったピンの組をスロットとして登録する（wire、pins、name、attach policy、console mechanism）。
3. serial port ごとに bind（流すストリーム 1 つ）を決める。
4. `save`。すぐ効く。再起動は要らない。

### 5.2 スロット

- 登録は「チップ」ではなく「場所」。チップを付け替えても登録は直さない。どのチップかは使うときに分かる（書き込みは板の家系と実際の
  チップの照合で守る）。
- **同時に持てる接続の数は probe が宣言する**（宣言が無ければ 1）。ロックは probe に 1 つのまま（接続ごとのロックは足さない）。複数の
  host の同時制御は transport の分離（TCP、host 側のブローカー）で、protocol の外。
- 登録の上限は protocol では決めず、probe が宣言する（ピンの数、保存の容量、ピン固定なら 1）。
- 仕様に要るもの: 数の宣言、今ある接続の一覧の操作、スロットの項目（今の bind が持つ pins と attach を移す）。

### 5.3 attach policy と確認

- policy は host / at boot の 2 つ。at boot に**再試行の間隔**（`retry_s`、0 なら再試行しない）を持たせ、いないスロットは
  probe がその間隔で止めない attach をやり直す。policy が host のスロットは確認しない。
- 「serial port が開かれたら attach」（on open）は持たない。開いたことは DTR でしか分からず、UART bridge・pty・DTR を立てない
  terminal では取れない。取れないことのある event に動きを結び付けない。IDE と pytest の monitor は ch32rv が自分で attach する
  （host）。素の terminal で見たいスロットは at boot にする（OEP の probe の止めない attach は動いているアプリを乱さず、クロックも
  変えないので、常に attach しておく代償は小さい）。
- **接続は使うときに作るのが基本。自動 attach は、接続に時間がかかる場合があるのを先払いするプリフェッチ。** 外れていれば使うときに
  attach からやり直す。登録に前回の速さを保存すれば短くなる。
- at boot のスロットの数は同時に持てる数まで（超える set は断る。順序は意味を持たない）。席が埋まっているときの host の attach は、
  bind だけが使っている接続のうちいちばん古いものを外して席を空ける（host も使っている接続は外さない）。押し出されたスロットは
  そのまま。再 attach は at boot の再試行か host の attach で起き、**bind はどの接続にも乗る**。
- **スロットの状態**（接続あり / いない（最後に試した時刻））は probe が describe にロック無しで出し、discovery でも
  OEP の client でも同じものが読める。host は線を駆動しない。線を駆動せずに「つながっているか」を知る方法は無い（内蔵プルでも、
  容量の戻り時間でも、接続ピンと未接続ピンで差が無かった）。
- 自動 attach は止めない attach だけ。

### 5.4 bind（serial port に何を流すか）

bind は serial port 1 つにストリーム 1 つ（スロットのコンソールか fixture UART の受信）。入力はそのストリームへ送る。

- 替えるときは host が bind を set し直す。セッションの間は進めず、終わったら止めた位置から続ける（あふれていれば一番古いバイトから）。
- 同じスロットを 2 つの port に bind してよい（x035-p4 の port 1 と port 3）。
- fixture UART のストリームは接続が無くても流れる。

## 6. IDE との結び付き

### 6.1 monitor

- この platform の protocol serial の pluggable monitor は **ch32rv**（`pluggable_monitor.pattern.serial`）。LinkE の CDC でも OEP の
  probe の serial port でも、`wchlink://` / `oep://` でも、monitor のプロセスは ch32rv。DESCRIBE の `source`（uart 既定 / sdi /
  dmdata / dmseq / rtt）をモニタの欄で選ぶ。
- ch32rv の monitor は、OEP の probe（と LinkE の debug の口を使う source）では**ブローカーの client** として console のストリームを
  受け、pluggable monitor の TCP へ流す（§7.2）。
- 設定の優先順は `--config` > profile の `port_config` > 板の既定（boards.txt `monitor_port.serial.<id>`）。`-m <profile>` のときは
  top-level の `default_port_config` は届かない。monitor が宣言しないキーを profile に書くと arduino-cli が exit 7 で止まる。
- 板の既定として `monitor_port.serial.chip=<家系>` を持たせ、ch32rv の monitor が §3.4 のスロットの選択に使う。

### 6.2 ch32rv の monitor に要る作り

- DESCRIBE のキーは `port_description`、列挙の値のキーは `value`（単数）。
- serial port の path を address に受け、`probe list` の `ports` から Link / probe を引く。source `uart` は CDC の素通し。
- **stdin の EOF で必ず終わる**（arduino-cli は CLOSE / QUIT を送らずに死ぬことがあり、tool は別のプロセスグループなので killpg も
  届かない。stdin を閉じると CLOSE は届き QUIT は届かない）。落ちるとき（接続の喪失など）は理由を data の行として流してから終わる
  （arduino-cli は tool の stderr を見せず exit 0 で終わる）。
- OPEN に error を返すと arduino-cli は exit 1 で stderr に tool の message を出す。

## 7. pytest の道具（最終の形）

### 7.1 家系

```text
pytest-embedded
 ├─ pytest-embedded-arduino-cli            upload / monitor
 │   └─ pytest-embedded-arduino-cli-ch32rv  ch32rv の path、broker endpoint、oep_host（oep-client-python を import）
 └─ pytest-embedded-wireskein              test ごとの ws_run、記録先はログのディレクトリ、teardown で verify（wireskein を import）

oep-client-python   純粋なモジュール（pip）。キャプチャを記録の受け口（callback）へ渡す口を持ち、wireskein に依存しない
wireskein           純粋なモジュール（pip）。runlog は標準ライブラリだけ、verify は numpy
```

- conftest は設定だけ。ch32rv のプラグインと wireskein のプラグインの結び付き（`oep_host` のキャプチャを `ws_run` へ）は、両方が
  入っているときだけの任意の連携。今の `tests/manual/oep_smoke/trace_kit.py` の `Run` はこれに置き換わる。
- **runtime は `dut` のまま、profile の platform が自前の monitor（`pluggable_monitor.pattern.<protocol>`）を持つときだけ自動で
  `arduino-cli monitor -m <profile> -p <port> -l serial --quiet` の子プロセスで受ける**（pyserial の URL handler として `dut` の裏に置く）。
  `pdut` は作らない。pyserial に固定する逃げ道は option。marker は使わない。tool を直接呼ぶ形は option としても持たない。close は
  stdin を閉じて少し待ち、終わらなければ SIGTERM。stdin を閉じていないのに stdout が EOF なら異常終了。
- 判定に要る情報は arduino-cli から取れる（`compile --show-properties --profile <p>` の `pluggable_monitor.*`。`board details -b` は
  profile を取らないので使わない）。fqbn は要らない（`-m <profile>` と cwd = sketch）。

### 7.2 ブローカー

probe の transport とセッションを持つのは**ブローカーだけ**。ch32rv の各コマンド（upload の flash、monitor、gdb、1 回だけの read /
reset）と pytest の `oep_host` は、どれもブローカーの **client**。

- ブローカーは**誰の子でもないプロセス**（ch32rv が切り離して起動する。stdio は捨てる）で、probe ごとに 1 つ。client は「この probe の
  ブローカーにつなぐ」と頼み、無ければ起動させる（起動の取り合いは DeviceLock と同じ利用者ごとの runtime ディレクトリの lock で
  1 つに絞る）。**client が 0 になったらすぐ終わる**（待ち時間は持たない。誰も使っていないのにプロセスが残らない）。
- 口は 127.0.0.1 の TCP で、OEP のフレーム（仕様の TCP の形）を受けて probe との 1 本のセッションに束ねる。client ごとに corr を
  付け替え、client の `open` / `end` は受け止めて probe に出さない。client ごとの資源（接続、plan、stream、止めた hart）を台帳に持ち、
  client が切れたらその分だけ外す。待ち受けの場所は runtime ディレクトリ（ch32rv の内部。利用者は
  `ch32rv broker endpoint --probe <sel> --json` に聞く）。
- **どの順で立ち上がっても同じ**。IDE は monitor を開いたまま debug を始められ、debug 中に monitor も開ける（§12）ので、monitor と
  gdb server はどちらが先でも同じブローカーの client になる。3 つ以上でも同じ。昇格や引き継ぎは無い（client が落ちても、その
  client の資源が外れるだけ）。
- 1 回だけのコマンドも同じ経路を通る（経路は 1 つ）。
- LinkE も同じ: ブローカーが LinkE の debug の口（vendor）を持ち、gdb と dmseq などの monitor を同時に使える。uart の source の
  monitor は CDC（TIOCEXCL で排他）だけを使うので、ブローカーにつながず probe の lock も取らない。

pytest の側:

1. upload / monitor: pytest-embedded-arduino-cli → arduino-cli → ch32rv（client）。
2. fixture: `pytest-embedded-arduino-cli-ch32rv` が port を `arduino_cli_resolved_port` から、ch32rv の path を pytest-embedded-arduino-cli
   が公開する展開済み properties の fixture の `runtime.tools.ch32rv.path` から取り、endpoint を聞いて（無ければブローカーを起動させて）
   `oep_host` を作る。monitor が付いていない probe（LinkE が書き、OEP の probe は fixture だけ、など）でも同じ。probe の指定は
   プラグインのオプション / ini。その probe の serial port をプラグインの peers として開かせてはいけない（pyserial が握る）。

仕様の変更は無い（probe から見える transport とセッションは 1 つのまま）。

### 7.3 ベンチの platform の入れ方

ベンチも利用者と同じ「index から入れる」形にする。作業ツリーの symlink（tool の依存が解けず `--build-property` / `--upload-property`
で補う形）はやめる。**local index**（`tools/index/install_check.py`: archive と index を作って loopback で配り、まっさらな data
ディレクトリに `core install`）は β を出す前の検査に使い、**日常のベンチは公開した β から入れる**。

## 8. ch32rv の範囲

書き込み・デバッグ・人が使うモニタ・discovery・ブローカー。OEP の probe への書き込みと monitor を足す（B）。**gdb / debug を OEP の probe で扱うのはプロトタイプの範囲外**（書き込みに要る DM の操作は `DtmAccess` に載せ、gdb はその上に後で）。 ただし**デバッグは次の作業で載せる前提**で設計する: OEP の transport は `DtmAccess` を実装し、ブローカーはセッションを長く保持する client（gdb server）を想定して client ごとの資源を追い、gdb server もブローカーの client（§7.2）。キャプチャや fixture
の仲介は持たない（試験の間は pytest の `oep_host` がブローカー経由で直接 OEP を話す）。OEP 対応の中身の設計は、この文書が固まった
あとに ch32rv の側で。

## 9. β のリリース

最終の形への節目。**暫定の形は作らず、最終の形に近いプロトタイプが端から端まで動いたところで β として出す。** 本番の利用者はいない
想定なので、β の後も破壊的変更を入れる。端から端まで = IDE で IDE port を選ぶだけで LinkE と OEP の probe に書けてモニタが見え、
pytest が upload → monitor → fixture（ブローカー経由の OEP）→ WireSkein の記録と照合まで通る。

| どこ | β までに |
|---|---|
| このリポジトリ | 板に `upload.protocol`。`upload.tool.<protocol>` → ch32rv（`--probe port:{upload.port.address}`）。`pluggable_monitor.pattern.serial` → ch32rv。discovery の登録。板に `monitor_port.serial.chip=<家系>`。ベンチを index から入れる形に。`trace_kit.Run` を pytest の道具に置き換える。`approval-status.ja.md` を埋める。index を作って公開 |
| ch32rv | §6.2、§7.2、§3.3、§3.4、OEP の書き込み（`requests/ch32rv.md` の 1〜14。15 は後） |
| oep-spec / oep-probe-arduino / oep-client-python | §11 の分 |
| pytest-embedded-arduino-cli、新しいプラグイン 2 つ、wireskein | §7 の分 |

## 10. 未決

- ベンチの pytest を最終の形（§7）に移す手順と順序（プラグイン 2 つと package 化が先）。
- ch32rv の OEP 対応の中身（ブローカーの corr の付け替え、`broker endpoint`、`port:` selector）。
- 切り離したブローカーが Windows / macOS でも生き残るか（IDE / arduino-cli が子を job object などでまとめて消さないか）。Linux は
  確認済み（§12）。1 回だけのコマンドがブローカーを通る分の遅れ。
- probe の HID の口（推奨の作り）。USB ID は取得済み（1209:4F45、§3.3）。
- 設定ページと JavaScript の client の置き場（OEP 側で決める）。

## 11. 各リポジトリに要る変更

| リポジトリ | 変更 |
|---|---|
| oep-spec | CDC / USJ のフレームを COBS に（core §3.1）。serial port の共用の規則（§4.2）。fn 0 の describe に経路の一覧。スロットの項目（今の bind から pins と attach を移す、target_id は任意、`retry_s`）、同時に持てる接続の数の宣言、接続の一覧の操作、スロットの状態。bind の組み直し（複数のストリーム、mode、選択）と対応 mode の宣言。生の転送の再開の位置。probe 開発ガイドに再起動の抑止と、専用 PID / HID の口の推奨 |
| oep-probe-arduino | serial port の共用（見分け、送信のキュー）。classic ESP32 に ProbeConfig（bind、NVS）。P4 を HS の口 + 専用 PID + HID の形に（USJ を COBS に、reset 抑止）。スロット、再試行、状態。bind の mode |
| oep-client-python | package 化。シリアルのポートは常に COBS、フレームの外は雑音。接続先の抽象化（serial / USB / ブローカーの TCP）。記録の受け口。`ch32_flash` は ch32rv の OEP の書き込みが入ったら消す |
| ch32rv | §6.2、§7.2、§3.3（`oep://`、`hid://`、後で `isp://`）、`--probe port:<path>`、OEP の transport と書き込み |
| pytest-embedded-arduino-cli | 展開済み properties の fixture。自前の monitor を持つ profile で runtime を `arduino-cli monitor` の子プロセスに（§7.1） |
| 新しいリポジトリ | `pytest-embedded-arduino-cli-ch32rv`、`pytest-embedded-wireskein` |
| wireskein | package 化 |
| このリポジトリ | §9 の β の分。`monitor_port.serial.chip`。ベンチを index から入れる形に。`trace_kit.Run` の置き換え |

## 12. 確認済み事実（決定を支えるもの。2026-09-29 の実測）

- Linux の tty は `TIOCEXCL` で 2 つ目の open が `EBUSY` になる。`arduino-cli monitor` が開いている間も `EBUSY`。root は素通り。
- classic ESP32 の UART bridge は、DTR と RTS を両方 on で開くと再起動せず、両方 off にして開くと再起動した。`arduino-cli monitor`
  の既定は dtr=on、rts=on。
- CH32 の debug の線は probe の内蔵プルに勝たず、何もつながっていないピンと同じに見える（X2）。ピンを一瞬駆動して内蔵プルで
  戻るまでの時間も、接続ピンと未接続ピンで差が無かった。止めない attach は動いているアプリを 0.5 µs の分解能で乱さず、約 170 ms
  かかる（X3）。
- arduino-cli 1.3.1: 板に `upload.protocol` が無いと素の upload は「A programmer is required」。`upload.tool.<protocol>` と port の
  properties（vid / pid / serialNumber）で経路を決められる。自前の discovery は `pluggable_discovery.<id>.pattern`、自前の monitor は
  `pluggable_monitor.pattern.<protocol>`。ch32rv の discovery を登録すると LinkE 7 台が `wchlink://<serial>` で並び、`upload -p
  wchlink://…` が programmer なしで書けた。
- pluggable monitor: tool との制御は stdio、データは TCP（OPEN <host:port> で arduino-cli が待ち受け、tool が接続）。`arduino-cli
  monitor --quiet` の子プロセスは両方向ともバイト透過（0x00〜0xFF、CR / LF）、tool → pytest の遅れ 0.4 ms、起動から最初のバイト
  まで約 1 秒（`-l serial` で 0.8 秒）。OPEN より前に stdin に書いたバイトは捨てられず、data の接続の直後に順のまま届く（OPEN を
  2 秒遅らせても同じ）。stdin が EOF だと設定の列挙だけで exit 0 してセッションを開かない。tool は列挙用と本番用で
  2 回起動される。tool は別のプロセスグループ。stdin を閉じると CLOSE が届き（QUIT は無し）0.02 秒で exit 0。DESCRIBE の列挙の
  値のキーは `value`。DESCRIBE の `protocol` は port の protocol と照合され、違うと OPEN 前に「invalid monitor protocol 'serial': only
  '…' is accepted」で exit 1（`--describe` の表示は通る）。設定の優先順は `--config` > profile の `port_config` > 板の既定、`-m` のとき top-level の `default_port_config`
  は届かない、変わった設定だけ CONFIGURE される、宣言に無いキーは exit 7。OPEN の error は exit 1 + stderr、セッション中に tool が
  落ちると stdout EOF + exit 0 + stderr 無し。`-m` のとき読む platform.txt は profile 用の写し（`~/.arduino15/internal/`）。
  OPEN の返事は 6 秒遅れても通るが、9 秒遅れると data の接続の直後にエラー無しで閉じる（exit 0）。tool が返事をしないと
  `Port monitor error: timeout waiting for message` で exit 1。
- Arduino IDE 2.3.10: upload の前は monitor を一時停止する（`notifyUploadStarted` → pause）が、debug の開始（`startDebug` →
  `arduino.debug.start`）は monitor に触れない。monitor を開いたまま debug を始めることも、debug 中に monitor を開くこともある。
- Linux: arduino-cli が起動した monitor の tool から、切り離した子（double fork + setsid）を起動すると、子は arduino-cli の終了後も
  生き残り（別の session、親は init に付け替わる）、arduino-cli の終了は遅れない。
- sketch.yaml の `default_fqbn` / `default_port` / `default_programmer` / `default_port_config` は CLI の flag なしで効く。
  `compile --show-properties=expanded` は `runtime.tools.*.path` を解いた形で返す。symlink で入れた platform では tool の依存が解けない。
- pytest-embedded-arduino-cli 1.6.0: upload は `arduino-cli upload --build-path … [--profile …] --port <port>` だけ。runtime の port は
  pyserial の `serial_for_url` で自分で開く。fqbn は扱わない。peers あり。device lock は port の path ごと。
- ch32rv 0.10.1: `probe list --json` は Link ごとに CDC の `ports` と `topology` を返す。`arduino discovery` は `wchlink://` を出す。
  `arduino monitor` の source は dmdata / dmseq / rtt。`isp` / `boot dfu|uf2|uart` は未実装。monitor の DESCRIBE は `port_descriptor` /
  `values` を返す（要修正）。
- 今の core の書き込み経路は 2 つ（LinkE の programmer、UIAPduino の HID）。core に USB CDC は無い（TinyUSB は取り込んだだけ）。
- factory ISP: BOOT ピンの家系と、ソフトからしか入れない家系（V003 / V00x、X035 / X315）。M030 は無い。UART は `57 AB` の framing、
  115200（wch-protocols）。
- OEP の仕様の今: 1 つの wire のインターフェースの connection は 1 つ（§5.2 で変える）。線の操作は scan / attach / detach /
  attach_under_reset で接続の一覧は無い。bind と describe の port は USB の CDC だけが対象（§5 で組み直す）。fn 0 の describe に
  経路の一覧は無い（§4.3 で足す）。X035 の治具で、CDC の口へのコンソールの bind が host の detach / reset を越えて続くこと、設定の
  保存（NVS、2 ms）が動いた。
- ESP32 系の probe の port は、DTR を立てたまま(pyserial の既定)なら開いても閉じても reset しない(P4 の USB-Serial/JTAG、
  CH340 越しの classic ESP32 と P4、CH343 越しの S3。2026-09-24)。DTR=0 で開くと reset するのは、pyserial が DTR を先に、
  RTS を後に書くので、途中で RTS=1 / DTR=0 = EN low を通るため。esp-idf-monitor は RTS を先に落とすので reset しない。
  RP2350(arduino-pico、USB は picosdk)はどの組み合わせでも reset せず、DTR=0 では CDC の出力が止まるだけ。Windows / macOS は未測定。
