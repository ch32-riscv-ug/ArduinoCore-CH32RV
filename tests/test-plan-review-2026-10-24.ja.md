# テスト計画のゼロベース再検討（2026-10-24 の事前整理、TODO から移した記録）

2026-10-29 に TODO.ja.md から移した。これを引き継いだ案は [TEST_DESIGN.ja.md](TEST_DESIGN.ja.md) と [BENCH.ja.md](BENCH.ja.md)。
プラグインの版（1.8.0 / 1.9.0）や件数（例 101 本など）は当時の値で、今は違う（1.10.0、例 117 本、Floating Point の選択肢 17）。
開いている作業は TODO.ja.md の「テスト計画」に残した。

## テスト計画のゼロベース再検討（2026-10-24、事前整理）

**2026-10-28: この検討を引き継いだ具体案を [tests/TEST_DESIGN.ja.md](TEST_DESIGN.ja.md) にまとめた。** OEP をファミリーごとに 1 台、
LinkE 複数台の前提で、profile の命名（`oep_ch32x035` など）と一覧、`--combo all`（plugin 1.10.0）での一括実行、`.env` での接続先の選択、
プローブのラベル（`<口>.<端子名>`）からの配線の取得と skip を案にした。採否は未定。以下は根拠として残す。

**目的は今後の比較に使う根拠と検討課題の準備。P4多配線での機能確認と、書込み・更新の
エコシステム確認を分けることを基本方針とし、具体的な構成、代表機、機材の購入、合格閾値は未決定。**
現行の [テスト計画](TEST_PLAN.ja.md)・[実行手順](README.ja.md)・
[カバレッジ](../docs/test-coverage.ja.md) を調べ直し、pytest-embedded-arduino-cli の profile を
中心に整理する案を比較する。既存 runner やディレクトリを残すことも、取り除くことも前提にしない。
今回は文書・ソースの調査と静的集計のみ。compile、書込み、実機測定、配線確認はしていない。
以下の「現状値」「仮定による計算」「未確認」を区別し、試算を実測や採用決定として扱わない。

### 基本方針：P4の多配線ベンチと、導入・書込み・更新の確認を分ける

2026-10-24 の追加指示による役割分担。**主軸はESP32-P4のOEP probeからDUTへできるだけ多く
配線し、周辺機能を外部から刺激・観測する試験**とする。
別の軸として、LinkE経由やUSB ISP等の書込み・更新を含む、Arduinoエコシステムの一連の流れを確認する。
同じDUTを使っても、目的・対象matrix・profileの選び方・結果を分ける。

| 試験の軸 | 主な構成と問い | 対象の増やし方 |
|---|---|---|
| 周辺機能の確認（主軸） | P4 OEP＋多配線DUT。GPIO/EXTI、UART、Wire/SoftWire、SPI/SoftSPI、timer/PWM/tone/Servo、ADC/DAC等の契約を刺激・観測できるか | series/part、instance、route、clock、容量、競合条件を増やす。配線はOEP labelで解決 |
| エコシステムの確認 | package導入、board/profile選択、build、対象発見、書込み、起動、monitor、次のimageへの更新、再接続まで通るか | LinkE、OEP、USB factory ISP、製品HID、対応するDFU等の利用者向け経路と、その経路の実装差分を選ぶ |

P4ベンチで使う書込みは機能試験の準備。そこで成功しただけでは他のupload経路の保証にはしない。
LinkEやISPで書けた後は、build ID・challenge応答・最小限の動作確認で新imageの起動を確認し、
全周辺機能を経路ごとに繰り返すことを基本にはしない。経路が初期化や残留状態へ影響する疑いがある場合に、
その差分だけ周辺契約を追加する。全周辺契約×全upload経路の直積を前提にしない。

USBも目的で区別する。USB device/hostのデータ通信・class・controllerの契約は機能確認側で、
USBからISPへ移行してimageを書き換え、アプリへ戻る契約はエコシステム側。
factory ISP、DFU runtimeの入口、DFU modeによる書込み、HID bootloaderを同じ成功としてまとめない。
P4で直接検証できないrole/速度/アナログ精度等は、必要なpeer・PC host・独立基準を機能ベンチへ追加する。
P4を主軸にすることと、P4単体で全観測を保証することは分ける。

エコシステム側で比較する一連のscenarioのたたき台:

1. 隔離環境に公開対象のpackage/core/toolsを導入し、FQBN・profileを選択してimage Aをbuildする。
   作業ツリーcoreでの機能開発試験とは入力を区別する。
2. 選択した経路で対象を見つけ、必要なbootloader/ISPへの移行を行い、Aを書き込む。
   初回導入と、動作中アプリからの移行を区別し、ボタン操作が必要な経路はその操作も記録する。
3. アプリへ起動・再列挙し、利用者向けmonitorとbuild ID/challengeでAの実行を確認する。
4. 異なるbuild IDを持つimage Bを同じ利用者向け経路で更新し、Bの起動・再接続・monitorを確認する。
   uploadコマンドの終了コードやISPへの列挙だけで更新成功としない。
5. core/tools/package自体の版更新後もbuild・upload・monitorを再現できるかは、firmware A→B更新とは別scenarioにする。
   通常更新に失敗したときの復旧入口・手順は、経路ごとの追加契約として整理する。

P4をエコシステム試験のreset操作や独立観測に補助利用する場合も、**検証対象の書込みをP4で代行して
LinkE/ISP成功と数えない**。補助経路なしでも利用者の手順が成立する範囲と、ベンチだけの補助を記録する。
未配線padをskipする規則は機能側の設備選択であり、LinkE/ISPの試験にP4のGPIO配線を要求しない。

profileと結果には「機能契約用build条件」と「導入/更新の経路」を別軸で残す。
上位ディレクトリを目的別に分ける案と、現行の設備別配置を維持してmarker/実行集合で分ける案を比較する。
この役割分担は基本方針とし、ディレクトリ名や追加optionはまだ決めない。

### 参照した作りと、現行計画から持ち越す問い

調査時の checkout は当コア `5af013beeb73`、`~/dev/EspUsbDevice` `b1094cd577b8`、
`~/dev/pytest-embedded-arduino-cli` `a50b0de8b47a`（いずれも調査開始時に変更なし）。
外部2リポジトリの最新公開版を調べた値ではなく、このローカル revision の内容を参照した。

| 参照 | 確認した構造・規則 | 当コアで比較したいこと |
|---|---|---|
| EspUsbDevice の `tests/README.ja.md`、`tests/TEST_PLAN.ja.md`、`tests/peer/usb_serial/` | 設備で single / loopback / peer / manual を分け、scenario ごとに sketch・pytest・profile を配置。peer は独立した sketch。released / local の相手を profile で区別 | 1契約を読むために辿るファイル数、実行コマンド、profile 追加時の変更箇所を現行 staging と比較 |
| 同 `tests/.env.example`、`tests/pyproject.toml` | profile 別の接続先を環境変数へ。通常の peer と local peer を分離。原則1 module 1 scenario、READY を再問合せ可能にする | 環境を切り替えても同じ契約・profile を使えるか。各チェックの初期化、停止、逆順確認をどう共通化するか |
| plugin の `TESTING_BASICS.ja.md`、`TESTING_ADVANCED.ja.md`、`TESTING_EXAMPLES.ja.md`、`README.ja.md`、`SPEC.ja.md` | DUT lifecycle、`peers`、profile / port の解決、build mode、module 単位の書込み、収集と skip、終了処理の責務 | plugin で足りる部分を再実装せず、CH32固有の upload・DMSEQ・identity 照合との境界を試す |
| 当コアの `tests/run_hardware.py`、`tests/harness/bench.py` | local TOMLから隔離 workspace、case.json、sketch.yamlを生成し、pluginへ渡す。共通設備ロックと個体照合を担当 | profile名がlocal target名になる現状と、環境に依存しないprofile名の比較。build-onlyにも設備設定を要求する入口を分けられるか |

現行計画には契約・設備・能力・結果の区別がある。一方、`docs/test-coverage.ja.md` には
「CDC/HID 等の sketch USB class は未実装」「Ethernet 等に公開 API があるとは扱わない」等、
同文書の別行や現行ライブラリ・実機入口と整合しない記述が残る。
`tests/TEST_PLAN.ja.md` の TinyUSB に関する記述も、撤去後の実装と再照合が必要。
この表から機械的に対象を選ばず、実装の条件・assertion・実行 artifact を別々に確認する。
EspUsbDevice の常設設備を前提とした通常実行も、そのまま当コアの既定動作にはしない。

### 規模の現状値と、組合せを増やす前の整理

以下は上記コアrevisionの静的集計。件数は工数試算の入力であり、カバレッジ率や達成目標ではない。

| 集計対象 | 現状値 | 読み方・限界 |
|---|---:|---|
| `boards.txt` の `.name` | 28 board ID | CH32系列27＋製品board `UIAPDUINO_V003_V14` 1。製品variantを別系列に数えない |
| `.menu.pnum.<part>=`（ANY除外） | 105型番 | seriesの下位軸。27系列×105型番とは掛けない。製品boardは別途扱う |
| `.build.march` の異なる値 | 5種類 | ISAの差分。これだけでABI・startup・FPU・割込み・linker差分を代表できるとはしない |
| `.menu.clock.<choice>=` | 20系列に計74選択肢 | 残り7系列の固定条件を加えると、汎用系列×clockは81セル。USB等が動くかは別条件 |
| 汎用系列の `opt` / `rtlib` | 各3 / 17選択肢 | clockも全積にすると1 sketchで `81×3×17=4131` セル。pnum・route・controllerはまだ含まない |
| `libraries/*/examples/*/sketch.yaml` | 101ファイル、profile定義617件 | 定義を数えただけ。build成功件数ではない。7つの通常profileに612件、H417専用4件、UIAPduino専用1件 |
| `tests/run_hardware.py` の `MODULES` | case名18、異なるPython module 17 | runtime / hid は同じmodule。case数とassertion数・機能数は一致しない |

再集計の最小例（repository root、ボード操作なし）。exampleの対象階層は既存profile生成器と同じ。

```sh
python3 - <<'PY'
from pathlib import Path
import re
b = Path('boards.txt').read_text()
print('board IDs:', len(re.findall(r'^\w+\.name=', b, re.M)))
parts = re.findall(r'^\w+\.menu\.pnum\.(\w+)=', b, re.M)
print('parts excluding ANY:', len([p for p in parts if p != 'ANY']))
print('ISA strings:', len(set(re.findall(r'^\w+\.build\.march=(.+)$', b, re.M))))
clocks = re.findall(r'^(\w+)\.menu\.clock\.\w+=', b, re.M)
print('clock series / choices:', len(set(clocks)), len(clocks))
profiles = list(Path('libraries').glob('*/examples/*/sketch.yaml'))
print('example files / profiles:', len(profiles),
      sum(len(re.findall(r'^  \w+:$', p.read_text(), re.M)) for p in profiles))
PY
```

現行の [profile生成器](../tools/generate/sync_example_profiles.py) は通常7系列を選び、
[要件判定](../tools/generate/sketch_requirements.py) は CLK EN マクロと ANY のFlash/RAM上限を使う。
これは最初のbuild絞り込みには使えるが、正確なpartの端子、公開API、USB role、配線の保証には足りない。
例としてX035の [variant](../variants/CH32X035/pins_arduino.h) は型番7種に対して
I2C1既定routeを「2 of 7 parts」と記し、I2C1は6 route、SPI1は4 routeを持つ。
`Wire` があることと、手元のpackageで既定のSCL/SDAが使えることを分ける必要がある。

### 系列差分をどの単位で扱うか

候補となる一行の単位は `contract × series/part/board × build条件 × 配線 × 観測方法`。
silicon、package、core実装、board引出し、fixture能力を別欄にし、
必要条件の積集合から実行候補を求める。未確認の能力を false と同一視しない。
profileを省いた理由も残し、単なる記載漏れで全件skipして成功する状態を検出する案を検討する。

下表は27系列を12群に並べた**差分調査の索引**。12台で十分という主張や購入リストではない。
根拠は [周辺機能表](../docs/peripheral-support.ja.md)、[USB controller一覧](../docs/usb.ja.md)、
`boards.txt`・`variants/`・各library。周辺機能表の空欄やCLK ENマクロだけでは有無を断定しない。

| 群（系列数） | 独立して拾いたい差分・境界 | 実験の候補 |
|---|---|---|
| V003（1） | ANYのRAM上限2048 B、rv32ec、10 bit ADC、timer共有 | 小buffer/OOM、ISRと通常処理、PWM/tone/Servo競合。UIAPduinoのpin map・HID uploadは製品契約として追加 |
| V002/V004/V005/V006/V007/M007（6） | rv32ec_zmmul、型番別容量・端子。V006のANYはRAM4096 Bで最大容量とは限らない | V003との差分、最小容量link、package別route。V006だけで6系列を代表できるかは未判定 |
| V103（1） | 旧USB IP、別startup/ABI・clock | USB device列挙/復旧、clock別UARTと時間。CLK ENにDAC等があっても公開実装の保証にしない |
| V203/V208（2） | USBFSとUSBD、型番別ETH・容量 | controller切替、単一/複数I2C、ETH対応partのclock条件（V208/V203RBは現行96 MHz条件と不整合の記載あり） |
| V205（1） | 端子別AF、追加UART、USBHS第2世代・PD | AF routeの実信号、追加instance、8 MHz既定条件と周辺の必要clock |
| V303/V305/V307/V317（4） | DAC、追加timer/UART、V303にUSBHSなし、他3系列はUSBHS第1世代 | 非対応controllerの拒否と対応側の通信を別契約に。DAC・timer共有・HSE条件 |
| V407/V467（2） | UART最大10、USBHS第2世代2基、ETH | instance末尾、controller 1/2、pin競合、ETH相互運用 |
| L103/M103（2） | ADC offset補正、M103のSPI公開範囲、USB/PDとclock | 独立電圧との照合、SPI1/他instanceの扱い、低clockでのPD。L103実機は既存TODOの復旧待ちを確認 |
| M030（1） | IWDGなし、ADC内部channel差、USBPD0/1差、PLL/HPRE | 非対応APIの結果、72/48/24/8 MHz条件、PD0実機。PD1を同じ成功に数えない |
| X033/X035（2） | X033にはCC端子なし。X035もC8T6等のhost対応とF8U6等の非対応がある | USB device/host/PDの3軸を分離。UART TX polling、1 I2Cでの独立masterによるslave試験 |
| X305/X315（2） | 端子別AF、ADC1〜4のpad分担、X305のSPI1欠如、X315のUSBSS | ADC instance選択、SPIの先頭instanceを仮定しない検査、SSは対応側だけ |
| H415/H416/H417（3） | HSADC、USBHSとdebug端子の共有、SSはH416/H417、ETHはH417 | FS/HS/SSと復旧経路、DMA/buffer欠落、ETH。2コア・sleep・永続化の設計実験は採用済み公開APIの回帰と分離 |

容量値は `boards.txt` のANY向けbuild上限で、siliconの総RAM・使用可能heapと同一ではない。
例えば64 Bのbufferを2本置くだけで、V003の2048 Bに対して `128/2048=6.25 %`。
同じ大きな統合sketchを全系列へ載せる案と、小RAM用に契約を分ける案を比較し、
bufferを縮めた場合は入力長・境界・保証範囲の変更も記録する。

代表選択は「各群1台」だけで終えず、候補partごとにカバーできる実装分岐・route・controllerと
追加できない条件を並べる。最小構成、全27系列を一巡する構成、package/instance差分を追加する構成で
必要台数・配線変更回数・未検証項目を比較する。未所持品と現在利用できない機材を分け、所持を推定しない。

### pytest・profile・環境設定の責務を比較する

設備別の上位分類（unit / platform / single / loopback / peer / instrumented / manual）と、
下位の機能・契約で整理する案は引き続き比較対象にする。系列ごとにテスト本文を複製する案とは分けて評価する。
PCのUSB host/計測器を使うケース、同一DUT内の折返し、別実装peerは観測の独立性が違う。
「DUTの枚数が1だからsingle」とだけ分類しない。難しい試験という理由だけでmanualへ置かない。

| 構成案（いずれも未採用） | profile / pytestとの関係 | 比較する費用と不足 |
|---|---|---|
| A: EspUsbDeviceに近い同居型 | scenario内に `.ino`、`test_*.py`、`sketch.yaml`、必要な `peer_<name>/`。明示pathとprofileでpluginを呼ぶ | 読みやすさ、単体再現。CH32の隔離core導入、個体照合、共有設備ロックをどこで行うか。通常収集で実機を開かない条件 |
| B: 同居型＋薄い設備adapter | Aに共通の設定検証・計測器・identity/lockだけを追加。compile/upload/port/peerの順序はpluginへ任せる | adapterの大きさ、標準optionとの競合、部分実行・build-onlyでも設備を要求しない構造 |
| C: 現行のstaging型を整理 | sketchとテストを分け、明示runnerが実行用workspaceを生成する | 隔離と実行証拠を保持する利点、ファイル追跡・profile差分・生成物debugの手間。手書きcase登録の増加 |

比較用の小さな題材は機能側のruntime、Wire master/slave、USBの片方向roleと、
エコシステム側のLinkE/USB ISPによるimage A→B更新とする案。
同じ契約・同じDUT・同じ入力で、設定行数、編集ファイル数、初回/再実行時間、原因特定の手順を測る。
既存fixtureのautouse化や、新しい汎用テストframeworkを先に作ることはしない。

設定の分担候補。2026-10-24 の追加要望により、**配線はOEP側で管理し、テスト環境には
接続先だけを渡す案を優先して検討する**。pin/channelごとの環境変数や、当コア側への配線の二重登録を避ける。
2026-10-25 の確認: **profile→OEP probe/口はplugin標準のprofile別環境変数で指定する**。
「環境変数を最小限にする」は配線・pin・機能有効flagを渡さない意味で、全profileを1変数にまとめる要求ではない。

| 保存先 | 入れる候補 | 入れない候補・検討点 |
|---|---|---|
| scenarioの `sketch.yaml` | 再利用できるprofile名、正確なFQBN/pnum、clock/route/必要build条件、固定peer core/library、console設定 | 個体serial、机名、ローカル絶対path。DUTの作業ツリーcoreと公開package検査を区別し、意図せずrelease coreを試さない |
| 共有の契約・能力定義（形式は未定） | contract ID、対象条件、観測、payload、境界値、必要fixture、対象外理由 | siliconの事実を手書きで二重管理しない。`sketch.yaml` に独自のpeer/capability schemaを混ぜない |
| `.env` | `TEST_SERIAL_PORT_<PROFILE>` でprofile→OEP probe/口を指定。peerもplugin標準の名前付き環境変数を使用 | 配線、pin/channel、機能ごとの有効flagは渡さない。別の対応表やprofile別port生成を必須にしない |
| OEP probeの保存設定 | 名前付きchannel label、idle/disable等、OEP仕様が提供する項目。DUT pad対応はlabelの命名規則で表す案を先に試す（下記） | labelは接続の宣言であり電気的導通の証明ではない。電圧等の専用metadata項目があるとは仮定しない |
| OEP側のhost設備設定（必要な場合のみ） | labelだけで表さないtarget identity、I/O電圧、電源/計測器、共有lock、校正値・配線revision | pad対応をlabelから解決できれば配線表を別に作らない。`bench.local.toml` に同じ配線を再記入しない。profileのpnum/clockと解決結果を照合する |
| CLI | 一時的なprofile/port/peer/試験選択 | override後もidentityと配線条件を検証。どこから解決した値かをartifactへ残す |

profile名の候補は、経路と対象を表す `OEP_CH32V003`、`LINKE_CH32V003` 等。
OEP側をP4によるperipheral機能確認、LinkE側を書込み・起動・更新等のエコシステム確認に使う。
part/role/controllerの違いがbuildや試験条件へ影響する場合は名前にも区別を加えるが、
case名・机名・個体名の全積にはしない。prefixは当suiteの命名規則であり、pluginが名前だけから
upload方式や試験用途を自動設定する機能ではない。実際のprofile設定・接続先と整合させる。
当コアで固定中の1.8.0は `--profile` の単一指定。2026-10-25にローカル1.9.0の
`--profiles` による組合せ実行を確認したため、下記では新機能と当コア/OEPの接続点を整理する。
共有profile生成、scenarioごとの明記、両者の併用を比較し、YAMLの独自継承機構は前提にしない。

plugin 1.8.0（当コアの `tests/uv.lock` と参照checkout）の規則として確認したもの:

- primary port: `--port` → `TEST_SERIAL_PORT_<PROFILE>` → `TEST_SERIAL_PORT` → profileのport。
  `--flash-port` はupload先だけで、console先のoverrideではない。
- peer profile: `--peer-profile name:profile` → peer側 `default_profile`。
  primaryのprofileは継承しない。peer portも `--peer-port name:port` →
  `TEST_SERIAL_PORT_PEER_<NAME>_<PROFILE>` → `TEST_SERIAL_PORT_PEER_<NAME>` → peer側profileのport。
- profile名の大文字化と `-` → `_` で環境変数名が衝突しないようにする。
  primary未解決は失敗、未解決peerはskip。必須peerのskipを合格にしない判定はsuite側の検討事項。
- `build_config.toml` の `[defines]` は環境変数名→define名。未設定は空文字になり得るので、
  必須pin/電圧/接続情報は操作前に検証する。数値pinを文字列defineのまま渡せるとは仮定しない。
  pad名を検証してheaderへ生成する案と、sketch側で解決する案を比較する。

接続先の例（個体IDは説明用placeholder）:

```dotenv
TEST_SERIAL_PORT_OEP_CH32V003=oep://<p4-unit-id>/v003
TEST_SERIAL_PORT_OEP_CH32X035=oep://<p4-unit-id>/x035
```

`LINKE_CH32V003` の接続先も `TEST_SERIAL_PORT_LINKE_CH32V003` で指定する。
値はLinkE経路のadapterが受け付ける接続先を検証して用意し、ここでは未確認のURI形式を定義しない。
**peripheral観測用の配線情報を必要とするのはOEP側の試験**とし、対象OEPの保存labelから
DUT pad→channel対応を取り出す。`.env` やprofileへpin/channelの配線表を追加しない。
LinkE側のエコシステム試験にはOEP配線の取得・必須判定を掛けない（書込み・console用の接続は別途必要）。
まずprofile名で試験用途を判定し、OEP対象だけ必要labelを照合する。取得済み設定に必要labelがなければ
該当試験を理由付きskipする案。label読出しと照合はsuite/adapter側の検討・実装事項である。

同じP4の別の口をprofileごとに選べる。同じbuild profileを別の机でも使い、接続先は `.env`、
配線差分はそのOEPのlabelから解決する。peerは `TEST_SERIAL_PORT_PEER_<NAME>_<PROFILE>`。
単一profileでは `--port` 等も使えるが、複数profileでは下記1.9.0の解決規則に従う。
CH32 adapter `pytest-embedded-arduino-cli-ch32rv==0.0.6` とDMSEQ・HID/OEP uploadの接続を試作で確認する。
`--run-mode=build` ではport・実機・校正値なしで成立するよう、build入力と設備入力を分離する比較も行う。

#### 複数profile実行：plugin 1.9.0の追加機能を利用する案（2026-10-25確認）

`~/dev/pytest-embedded-arduino-cli` `5ef1658dfadc` のCHANGELOG、仕様、実装、テストを確認した。
ローカルcheckoutの版は1.9.0。当コアの `tests/uv.lock` はまだ1.8.0で、依存更新・実機移行はしていない。
公開配布先の提供状況は今回の調査対象外。以下はローカル1.9.0に対する評価。

**前回検討した「pluginが複数profileをpytest itemへ展開する」機能が追加されており、今回の用途に
利用できる見込みが高い。** 汎用の展開・結果集約を新runnerで再実装する必要性は下がった。
今後は1.9.0を使ったCH32/OEPとの接続を比較・検証する。suite構成全体の採用決定や移行完了とはしない。

| 必要だったこと | 1.9.0で確認した提供範囲 | 当コア側に残ること |
|---|---|---|
| 複数profileを一度に指定 | `--profiles=名前,名前` / `--profiles=all`。peerは `--peer-profiles` で独立指定。単一指定の従来動作も保持 | 機能用・エコシステム用の対象集合を選ぶ |
| 組合せごとの実行と集計 | primary/peerを含むitem ID、DUT lifecycle、log/artifact、JUnit、検証状態を分離 | contract・配線・観測の情報を結果へ関連付ける。job間集約は外側に残す |
| 部分実行・失敗再実行 | 組合せを含むnodeid、`-k`、同じ候補指定での `--lf` | 実行予定/未完了、必須契約のskipを確認する |
| build前の対象絞り込み | `arduino_skip_if(predicate, reason=...)` と `pytest_arduino_cli_skip_reason(item, combination)`。理由は加算、判定の例外/不正値はerror | 系列/part/role/配線の適用条件を純粋な判定として渡す |
| 設備不足と誤割当 | 組合せmodeではport未設定・同じ構成内の重複deviceをcompile前にskip。設定済みportの接続失敗等はerror | OEP/LinkE/ISPの別addressが同じDUTを指す場合のidentity、共有P4/電源の排他 |
| build-onlyと収集 | build-onlyはport不要。collect-onlyはbuildやartifact削除をせず、適用条件のskipを表示 | collect-onlyのhookにも実機I/Oを持ち込まない。port/重複deviceの実行前確認はcollect-onlyでは行われない |
| 意図しない増殖・並列化を防ぐ | `.ino` のないunitは複製しない。peer不使用のtestはpeer候補で増えない。組合せmodeでxdistを拒否 | 実機を逐次実行し、別process/別toolとの共有設備lockは引き続き必要 |

1.9.0のoptionを使う入口の例（`<scenario-path>` とprofileは移行時に用意するもの。
現行 `run_hardware.py` をこのコマンドへ置き換えるだけでは動かない）:

```sh
pytest <scenario-path> --profiles=ch32v003,ch32x035,ch32v307 --collect-only -q
pytest <scenario-path> --profiles=ch32v003,ch32x035,ch32v307 --run-mode=build
pytest <scenario-path> --profiles=ch32v003,ch32x035,ch32v307 --junitxml=result.xml -rs
```

実際のitem IDは `test_gpio[primary=ch32x035]`、peerを含む場合は
`test_usb[primary=ch32x035,usbhost=s3_host]` のような形。
`--profiles=all --peer-profiles=all` は各peerも含めて直積を作るため、P4機能試験の既定にはしない。
primaryのlistだけを指定し、peerは必要なdefault/明示指定を使う。配線済みの組だけ試したい場合は、
候補を絞ったうえでmarker/hookから不成立の組を理由付きで除外する。
LinkE/ISPのimage A→B更新は一つのscenario内の順序として維持し、独立itemの実行順へ依存させない。

**環境変数を最小限にする方針との接続点**:

- 複数候補を持つprimaryでは共通 `--port` / `--flash-port` が設定エラーになり、
  `TEST_SERIAL_PORT` の共通fallbackも使われない。peerも複数候補では同様。
  現在利用できる指定元はprofile別環境変数または `sketch.yaml` の `profiles.<profile>.port`。
- 当コアでは標準のprofile別環境変数を使う。profile→OEPの別の対応表、portを埋め込んだ
  `sketch.yaml` の自動生成は、この対応付けのためには不要。共有profileへ個体addressを固定しない。
  配線名はOEP labelから取得し、pin/channelの環境変数を増やさない。
- 接続先解決は標準pluginへ任せ、既存の隔離/staging処理を薄く整理できるか確認する。
  現行runnerはtarget/caseごとに単一profileを生成してpytestを起動するため、
  複数profileを持つscenario workspaceへまとめる工程が必要。
- 同じDUTでclock等を切り替える別profileは、profileごとに同じportを明記して逐次利用できる。
  異なるchipのprofileを一つの口へ誤って対応付けないようidentity/partを照合する。

**OEP配線名によるskipとの接続点**:

- skipの範囲と判定時点は試験内容に応じて決める。配線不足を一律に収集時の全体skipにしない。
  profile/系列が対象外、または試験全体の必須配線が欠けると事前に分かる場合は、収集時に
  該当item（必要ならscenario内の全item）へskipを付ける候補。fixtureや実行中の情報が必要なら
  setup/本文で判定する。一部のGPIO padやI2C routeだけ不足する場合はその確認項目だけを対象外にし、
  独立して成立する他の項目は続ける。範囲（全体/項目）と時点（収集/setup/本文）は別の判断軸とする。
  通常のテスト本文で `pytest.skip()` を呼ぶと、そのitemの実行が終了し、後続の確認には進まない。
  項目別のpass/skipを残す必要がある場合は、別test/parametrize/subtest等を比較する。
  単なる条件分岐で処理を飛ばしただけではpytestのskip件数に入らないため、結果の粒度も併せて検討する。
- 新しいmarker/hookは収集時に呼ばれ、fixture取得・機材接続を行わない契約。
  hook内で `oep_host` を取得してlabelを読めるわけではない。`combination` はprimary/peerの
  profile名を持つ読み取り専用値で、解決済みの機材・配線を内包しない。
- 案1は、profile別環境変数から対象OEPを解決し、収集前の設備preflightでlabel/能力/identityを一度読む。
  hookはその実行中のメモリに保持した情報とprofile/variantで判定する。別の配線表や永続snapshotは必須にしない。
  取得の入口と `--collect-only` での機材照会の扱いは別途検討し、hook自身には機材I/Oを入れない。
  情報未取得と「取得済みだが必要labelなし」を区別し、label取得だけを導通確認済みとは扱わない。
- 案2は、実行時にbuild/uploadより前へ配線preflightを接続する。現行CH32 adapterの `oep_host` は
  `dut` に依存するので、そのfixtureを本文で使ってskipしても書込み前判定にはならない。
  pluginのfixture順序とadapter側の小さな追加で成立するか別途確認する。
- 取得後に設定/identityが変わった場合の扱い、実行直前の確認、機材読出し失敗をskipへ変換しないことを残課題にする。
  port未設定で全件skipしてもpytest終了値は成功になり得るため、必須契約の実行有無は結果で確認する。

確認根拠はpluginの `combination.py`、`plugin.py`、`tests/test_combinations.py` と
`TESTING_ADVANCED.ja.md` 冒頭の組合せガイド。2026-10-25に次を実行し **41 passed（5.96秒）**。

```sh
# plugin checkout内。ローカルsourceを隔離環境へ導入し、機材を模擬した検査だけを実行
uv run --isolated --no-project --with /home/mt/dev/pytest-embedded-arduino-cli \
  python -m pytest tests/test_combinations.py -q
```

この41件は模擬compile/upload/peer等によるpluginの検査で、CH32の実compile、OEP/P4、LinkE、ISPを
動かした結果ではない。追加検証は当コアの1.9.0への依存更新候補、CH32 adapterのprofile切替、
2 profile×2 scenarioでの実行・配線skip・失敗後の次profile・共有P4排他に絞って計画する。

### OEPの名前付き配線で選択・skipする案（追加要望）

追加で読んだローカル資料は `~/dev_oep/oep-spec` `cc82b5e697d4` の
`docs/host-development-guide.ja.md` §5.3 / §15、`docs/oep-core.ja.md` §13.8 と、
`~/dev_oep/oep-client-python` `b32731341c70` の `src/oep_client/settings.py`。
probeに `channel.{}.label` を保存し、`S.N` の `S` を口、`N` を線として参照できる。
clientには `labels()`、`ports_named()`、`line_from_labels()` / `find_line()` がある。
`rx` はOEP側の受信（DUT TXへ接続）、`tx` はOEP側の送信で、向きを取り違えない。
汎用I2C/SPI配線名とDUT pad対応の自動解決まで実装済みという意味ではない。

OEPの現在の仕様では接続方法・UART実行時設定等はhostの責務で、**配線情報をすべてprobe内に
保存できるとはしない**。「OEP側」はprobeのlabelとOEP側hostの設備管理を含む候補とする。
当コアの [OEP連携文書](../docs/oep-workflow.ja.md) は旧設定仕様も含むため、採用中のclient/plugin/probeで
利用できる項目と再照合する。OEP clientの構成設計文書 `docs/hardware-configuration.ja.md` も
設計案であり、そのschemaを現行runnerが読めると扱わない。

| 所有する情報 | 具体例（追加の名前は案） |
|---|---|
| OEP probe | `S.rx`、`S.tx`、`S.nrst`。追加候補 `S.i2c_scl`、`S.i2c_sda`、`S.spi_sck`、`S.spi_miso`、`S.spi_mosi`、`S.spi_cs` |
| OEP側の設備管理 | `S.i2c_scl` がDUTのPA10へ、`S.i2c_sda` がPA11へ接続、I/O電圧・pull-up・対象part・配線revision。別のboardでは同じ役割名から別padへ解決 |
| テスト契約 | Wire master試験は `i2c_scl` / `i2c_sda` と独立I2C target能力を要求。SPI試験は4信号とslave/観測能力を要求。物理channel番号は知らない |
| 実行時の指定 | 使用するprobeと口を選択。契約側の相対名をその口の中で引く。別probeの同名信号を混ぜない |

上表の役割名＋別の対応表に加え、下記のDUT pad名だけで済む案を先に試す。
probeのGPIO番号・机名・全機能の有無を名前に詰め込まず、任意のlabelだけで電圧・方向・peer能力を推定しない。
DUTのpad対応は命名規則またはOEP側の対応情報から解決し、probe channel番号をArduino pin番号として使わない。
永続化はprobeのchannel名/labelを使い、そのbootでの数値channelへ実行時に解決する。

試験全体の必須条件を事前判定する場合の流れの候補は、**接続先選択 → label/設備情報の読出し → 必要な名前と能力の照合 →
実行する契約とskip理由の表示 → 対象契約だけbuild/upload/plan/観測**。
pytest本文で `dut` を受け取った後に判定すると書込みが先に走り得るので、DUT lifecycleより前の
preflight/fixture依存関係で解決できるか試作する。一部項目だけを省く試験や実行時に条件が判明する試験は、
共通のbuild/upload後に項目ごとに判定する候補も残す。build-onlyはこの機材照会を行わず独立に実行する。

| 状態 | 扱いの候補 |
|---|---|
| 正常に読めた設備情報に必要な線/任意機材の名前がない | その契約をskip。例: `missing wiring: S.i2c_sda`。他の契約は続ける |
| 名前はあるが一部のpad対応・電圧等が未定義 | 明示的な「未整備」なら理由付きskip。不正な値・矛盾した登録なら設定エラー |
| 名前が揃っているが必要なpeer/計測能力がない | 能力不足としてskip。線があることだけで実行可能にはしない |
| 指定probe/portに接続できない、設定読出し失敗、名前の曖昧さ、identity不一致 | 環境/設定エラー。「配線名なし」へ変換しない |
| 設定取得APIが未対応 | 対応可否を別に記録。`labels()` が空配列を返す場合もあるため、未配線との区別が必要 |
| 登録済み配線で応答・観測が期待と違う | FAILまたは根拠のある設備エラー。試験途中のtimeoutを配線不足のskipにしない |

名前は人が配線を登録した宣言なので、抜けた線を常に自動検出できるわけではない。
配線変更はOEP側の設定とrevisionを更新し、実行時にはlabel・解決したpad/channel・能力・設定hashを
artifactへ記録する。テストからprobeの保存設定を自動で書き換えず、planは実行中だけ確保・解放する。
開発時は未配線をskipし、将来の必須実行集合では必要契約がskipされたことを別に報告する案。
skipを避けるために `HAS_I2C=1` 等の環境変数を増やさない。

最初の試作で測る値は、接続先以外の環境変数が不要か、配線変更時に編集する正本の数
（OEP側のみを目標）、label照会時間、preflight後のskip件数と理由。
I2CのSDA名を未登録にした構成、正常構成、登録済みだが相手が応答しない構成で、
それぞれskip・実行・失敗に分かれるかを確認する。方式・OEP側の保存形式はまだ確定しない。

#### 現行OEP仕様のlabelだけで配線対応を表す案

**DUT padとの1対1の配線対応は、既存labelの命名規則で表現できる。別の配線schemaやOEP protocolの
追加は必須ではない。** 追加するのはテスト側の名前の解釈とskip判定であり、現行pluginが自動対応するわけではない。
core §13.8ではlabelは1〜32 byte、probe内でASCII大小文字を無視して一意。
host guide §5.3の `S.N` は、Sが英字で始まる英数字/hyphenの1〜16文字、
Nが英数字/hyphen/underscore、dotは区切り1個。この範囲で次の案を比較する。

| 案 | 保存するlabelの例 | テスト側の解決方法・得失 |
|---|---|---|
| pad名だけ（最初に試す候補） | `S.pa10`、`S.pa11`、`S.pa5` | 選択した口Sのchannelが、それぞれDUTのPA10/PA11/PA5へ接続しているという宣言。機能/方向はprofileで選んだrouteと契約から決める。同じ配線をGPIO/PWM/I2C等で再利用しても改名不要 |
| 役割とpad名 | `S.i2c1_scl_pa10`、`S.i2c1_sda_pa11` | 役割とDUT padをテスト側で分解する。別の対応表を省けるが、役割変更時の改名、routeとの矛盾、複数用途への再利用を扱う必要がある |

pad名だけの例では、X035のI2C1 route 0を対象とする試験はvariantからSCL=PA10/SDA=PA11を取り、
`S.pa10` / `S.pa11` が揃えばそのOEP channelへ解決する。どちらかがなければ当該routeの試験をskip。
同時に、正確なpartでそのrouteが存在すること、OEPに必要な刺激/観測能力があることを確認する。
任意の配線から利用可能routeを列挙する案も比較できるが、指定routeがないときに別routeへ黙って替えず、
実行したrouteとskipしたrouteをそれぞれ記録する。build/profileが固定するrouteを名前だけで変更しない。
`S.pa10` は6 byte、`S.i2c1_scl_pa10` は15 byteで、どちらも現行labelに収まる。
長い口名も含め全体32 byte以内を検査し、大文字小文字の違いを別配線にしない。

既存ツールとの共存には次の条件がある。

- `S.swdio` / `S.swclk` / `S.swio` / `S.nrst` と、UART helperが使う `S.rx` / `S.tx` は
  そのまま残す。`S.rx_pa9` に変えると、現行の `line_from_labels(..., "rx", "S")` では見つからない。
  確認元は `~/dev_wch/pytest-embedded-arduino-cli-ch32rv` `7c1142e797a1` の `plugin.py:fixture_uart`。
- 1 channelにlabelは1個なので、同じ線に `S.rx` と `S.pa9` の別名を同時保存できるとはしない。
  初回は標準labelの線と汎用pad labelの線を分ける。同一線を共用する場合は、profileのUART routeから
  DUT padを解決する案、またはhost側helperの解釈を拡張する案を別途比較する。OEP仕様変更とは分ける。
- `pa10` はDUT側pad名。OEP側GPIO名は設定のkey `channel.<GPIO名>.label` にすでにあり、labelへ重ねて書かない。
  存在しないpad、同じDUT padへの曖昧な複数接続、profileとの不整合は設定エラーとして検査する。
- 電圧・pull-up・USB VBUS・複数targetへの分岐・外部計測器の校正まで、短いpad名から推定しない。
  固定治具の共有条件で足りる範囲から試し、可変になる情報だけOEP側の設備管理へ残す。

TST-04ではまずpad名案で、**OEPの保存label＋既存profile/variant＋接続先指定だけ**で
GPIOとI2C routeの選択・skipが成立するかを確認する。fixture要求を満たさない場合のskipと、
名前を読めない/登録済み配線で通信が失敗した場合のエラーは前表のとおり分ける。

### 機材・配線のたたき台

以下は**P4 OEPを中心に1つの試験を逐次実行する機能bench**の候補数量。所有状況、製品型番、購入額は未調査。
同時接続できるかはpart/package/board回路とデバッグ・consoleの端子競合を照合してから決める。
常設の多配線を基本に、P4の使用可能channel・role・同時動作数と、DUTの端子/電圧/資源競合を照合する。
P4の総GPIO数をそのまま使用可能本数とせず、firmwareの宣言・予約端子・実際の引出しを数える。
配線できるが同時には駆動できない組はplanを切り替えて逐次試験し、物理的に両立しない配線だけ
番号付きハーネスや切替治具を比較する。多く配線することと、全機能の同時実行を区別する。

| セット | 機材候補・数量 | 配線案と追加して観測できること |
|---|---|---|
| 起動・復旧 | DUT 1、P4 OEP 1、給電元1、必要時USB-UART 1 | SWIOまたはSWDIO/SWCLK、GND、VTref（sense）、NRST。BOOT端子は利用可能なboardで引き出す。被測定UART/USBとは別のconsoleを確保 |
| GPIO・時間・UART | P4のfixture/captureを基本に利用。不足する能力に応じて独立peer/logic analyzerを追加 | DUT TX→P4 RX、DUT RX←P4 TX、DUT OUT→P4 IN、DUT IN←P4 OUT、共通GND。刺激と観測を別にし、EXTI・pulse幅・baud・PWM/tone/Servoを測る。必要channel数/帯域は下記試算と照合 |
| I2C / SoftWire | master/slaveとNACK/stretch/stuck操作が可能なpeer 1、切替式pull-up 2本 | SDA同士、SCL同士、GND。単一I2CのDUTもroleを替えて試せる。GPIO出力の直結ではなくopen-drainで異常を注入し、終了時解放 |
| SPI / SoftSPI | 既知payloadを返す独立SPI slave 1（peerと兼用候補） | SCK/MOSI/CS: DUT→peer、MISO: peer→DUT、GND。MOSI–MISO jumperは別のloopback契約。CS・mode・bit orderの保証にはpeer/波形が必要 |
| ADC / DAC / HSADC | 安定した可変電圧源1、独立DMM 1、必要なら波形源/oscilloscope各1 | 電圧源→ADC、DMMで同じ入力とVDDAを観測。DAC→DMM/既知負荷。DUT内DAC→ADCだけでは絶対精度を証明しない。HSADCは入力帯域・DMA欠落を別契約化 |
| USB FS相互運用 | USB role対応peer 1（ESP32-S3等は候補）、DUTとpeerそれぞれの独立console、データケーブル1組 | host D+/D−↔device D+/D−、GND、VBUSの給電元1つと切断点を定義。DUT host / DUT deviceの2配線・2profile。PC hostも独立の相手として残す |
| USB HS/SS | 対応DUT/コネクタ/ケーブルとPC host各1系統 | FS peerや安価なGPIO captureを流用してHS/SSの電気品質を保証しない。controller別に接続し、H41xのdebug共有端子と復旧手段を確認 |
| PD | PD sourceまたはsink 1、CC/VBUS breakout 1、電圧計1、必要時電子負荷・電源制御各1 | CC1/CC2とGND、VBUSは対応する電源回路と計測入力へ。DUT GPIOには直接入れない。向き2通り、交渉ログと独立実電圧、取り外し/Resetを別観測 |
| ETH・reset/電断 | Ethernet相手PC/固定server、switch/RJ45、個別電源スイッチ各1系統 | DHCPあり/なしを固定。電断時はprobe/USB/信号からの逆給電も確認し、VDDを測る。USB再列挙だけをcold bootとしない |

I2C/SPI等のpeer機能もP4の採用firmwareが提供する範囲を先に調べ、不足する機能だけ追加機材で補う。
LinkE・ISP用USB/boot操作・製品HID等の機材と配線は、上表の全信号を複製せず、
エコシステム用の経路表として別に準備する。各経路のDUT、boot入口、書込みtool、起動/monitor、
更新後の識別、復旧手段、人の操作の有無を一覧化する。

X035の配線下書き例はSPI1のSCK=PA5、MISO=PA6、MOSI=PA7、I2C1既定のSCL=PA10、SDA=PA11、
ADC入力候補PA0（variantの定義から）。**CS・GPIO・NRST・peer側端子は未割当**。
特にI2C既定routeは全packageにないため、C8T6等の正確なpad引出しとboard回路図を照合し、
必要なら定義済みの別routeへ替える。board上の「D番号」とpad名は別欄にする。
配線台帳には `signal / DUT pad / connector pin / peer pad / direction / voltage / pull-up / idle /
resource(timer, bus, debug) / harness revision` を持たせる案。

信号数の試算は UART 2＋I2C 2＋SPI 4＋GPIO入出力2＝**デジタル10本**、別途ADC入力1・
debug/reset・GND。全信号の同時観測には8chでは不足し、16ch案とバスごとの付け替え案を比較する。
3.3 Vを仮定したI2C pull-up候補は4.7 kΩならLow時約0.70 mA、2.2 kΩなら約1.50 mA。
実装済み抵抗との並列合成、I/O許容sink電流、配線容量、立上り波形から決める。抵抗値は未確定。
両側を独立給電する案ではGNDを共通にし、電源出力同士を直結しない。

### 数値を集める実験と判定の設計課題

| 論点 | 仮定した試算・初回の測定案 | まだ決めないもの |
|---|---|---|
| buildの総量 | 現行617組を1組20〜60秒と仮置きすると逐次約3.4〜10.3時間。101例×28 boardの無条件積は2828組で、現行定義の約4.6倍。ただし対応不可も含む | 全積採用、CI頻度、実所要時間。cold/warm cache、download、link、失敗を別計測して見積り直す |
| 実機時間 | 仮に12対象×8 scenario×3反復=288 session、1 sessionをbuild20＋upload10＋READY5＋刺激10＋終了5=50秒と置くと4時間。手動配線時間とpeer build/uploadは別途加算 | 12対象や3反復を必要十分とはしない。中央値/p95、失敗時の復旧時間、cache再利用を実測する |
| 初回の取得量 | 代表2〜3対象で各10 sessionを測る案。cold build、warm build、upload、READY、観測、cleanupを分離し、配線交換も10回記録 | 10回で安定性を保証しない。p95等にはsample数も併記し、少数標本で裾の精度を主張しない |
| UART・SPI | 115200 baud・8N1の64 Bは片道 `64×10/115200≈5.56 ms`。SPIを10 MHzで10 sample/bit観測する案なら100 MS/s | host/DMSEQ/USB遅延をbaud誤差へ混ぜない。10 sample/bitは測定器選定用仮定で、電気特性の合格基準ではない |
| 時間分解能 | 24 MS/sなら1 sample≈41.7 ns。2 edge差の保守的な量子化幅を2 sampleとすると、1 µsパルスに約8.3 %、100 µsに約0.083 % | 許容jitter/精度。計測器のtimebase誤差、しきい値、trigger、負荷を加えた誤差予算を先に作る |
| ADC基準 | VDDA=3.3 Vと仮定した1 LSB幅は10 bitで約3.22 mV、12 bitで約0.806 mV。入力点候補はVDDAの10/50/90 %、各100 sample | ±何LSBで合格か。VDDA実測、reference/DMM精度、noise、source impedance、sample timeと温度を先に記録 |
| buffer境界 | 容量Bに対して0/1/B−1/B/B＋1/2B＋1、USBではpacket境界も別に取る。ISR/foreground、reset/切断で保持・破棄の契約を確認 | Bや全系列共通payload。B＋1が正常・拒否・部分転送のどれかはAPI契約から定める |
| 反復と信頼度 | 独立・同じ失敗確率を仮定し0失敗/n回なら、片側95 %の上限は `1−0.05^(1/n)`。n=100で約2.95 %、n=1000で約0.30 % | 100回成功を高信頼と呼ばない。共通電源・同じ初期状態の相関を含む連続試験にそのまま適用しない |

build、P4中心の周辺機能、エコシステムの経路別E2Eのmatrixを別々に作る。候補は「共通API＋実装分岐の代表を普段実行」「全系列の基本build」
「part/linker・route・clock・controllerの差分」「長時間/復旧/相互運用」を別の選択集合にすること。
最小・既定・最大clock、最小RAM、別ABI、追加instanceの端を選ぶ案とpairwise案を比較し、
USB×clockやtimer×PWM/tone/Servo等の既知の相互作用は個別に残す。
将来API（sleep/永続化/2コア等）は設計実験の入力として保持し、現行公開APIの必須回帰と混ぜない。

結果の候補欄は、contract ID、期待する適用条件、実行profile/FQBN、core/peer/tool revision、
設定の出所、配線revision、観測器の設定、入力/seed、時間、実測値、許容差の根拠、ログ/波形。
`対象外（理由付き）/未実装/テスト未定義/設備不足/未実行/環境エラー/PASS/FAIL` を分ける。
未対応APIの拒否動作を確認する契約は、対象外にせず、その期待結果でPASS/FAILを付ける。
pluginの最後のprofile別結果やJUnitだけから、全partや全配線の保証を推定しない。
retryで通った結果は初回失敗を消さず併記し、READY失敗を無条件に設備のせいにしない。

### 次の検討TODOと、比較を始めるための成果物

- [ ] **TST-01 現状の照合**: 計画・coverage・library実装・runner・実行artifactの不整合を一覧化する。
  各契約を再利用/作り直し/対象外候補に分け、理由を書く。文書上の実機確認と今回の未測定を混同しない。
- [ ] **TST-02 能力表**: 固定device-dataの系列/part/package/routeと実装条件からmatrixの試作を作る。
  27系列・105型番・製品boardを照合し、ANYによる近似、clock、role/controller、追加instance、未知を表現する。
- [ ] **TST-03 profile比較**: A/B/Cそれぞれでruntime・Wire・USBの同じ契約を配置する小試作を作り、
  profile追加、1件だけ再現、build-only、peer変更、通常収集、終了処理の手間を比較する。まだ移行しない。
  1.9.0で追加された組合せ実行を使い、2 profile×2 scenarioでCH32 adapter・port割当・配線判定を検証する。
  pluginの汎用展開/集計は再実装せず、既存runnerの整理範囲と、setup失敗・skip・再実行・共有P4排他を確認する。
- [ ] **TST-04 OEP配線名と環境切替**: profile別の標準環境変数で接続先を指定し、同一profileを2つの
  OEP設備で解決する。全体の必須配線不足をDUT lifecycle前にskipする例と、一部項目だけをskipして
  他の項目を続行する例を比較する。未対応設定API・読出し失敗・登録不正とは区別する。
  まず現行labelの `S.pa10` 等からDUT padを解決する案を試し、標準debug/UART labelとの共存を確認する。
  labelに含めない電圧/peer/lockのOEP側管理、解決結果の保存、秘密値の除外を検討し、配線表の追加を必須にしない。
- [ ] **TST-05 機材台帳と配線**: 所持品、故障/復旧待ち、未所持、測定能力不足を区別する。
  代表partの回路図、NRST/復旧、I/O電圧、debug競合を確認し、OEP配線名と対応するハーネス図・端子表・信号数・交換手順を作る。
  P4の使用可能channel/role/同時動作数を数え、常設の多配線とplan切替で覆える範囲を整理する。
  LinkE/ISP等はboot→書込み→起動→monitor→更新→復旧の経路表と必要最小配線を別に作る。
  最小benchと差分追加benchの数量・実際の見積額・保守費を比較してから機材案を見直す。
- [ ] **TST-06 初回測定**: 上表の時間・容量・信号・誤差を小規模に実測し、仮定値を置き換える。
  外部peerによるWire slave/repeated START、独立基準による時間/ADC、非対応partの拒否を含める。
- [ ] **TST-07 実行集合と証拠**: 普段/全系列build/差分実機/長時間を候補として、必要契約・skipの扱い・
  所要時間・未検証範囲を比較する。通常順、単独、逆順、途中失敗後の復帰でも再現できることを調べる。
  P4での周辺機能確認と、LinkE/ISP等のimage A→B更新・package更新の結果を分離し、全経路×全周辺の直積を避ける。
- [ ] **TST-08 計画改訂のレビュー**: 能力matrix、A/B/C比較、profile例、環境schema候補、機材/配線案、
  実測と未確認事項を揃えて [テスト計画](TEST_PLAN.ja.md) の改訂案を作る。
  方式の採否、代表機の確定、旧入口の廃止、実装順はその時点の検討に残す。
