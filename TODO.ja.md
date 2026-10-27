# TODO

決まったことは ADR や各文書に移し、ここからは消します。

全体の作業順、成果物、判断条件とモデル間の引き継ぎは
[コア全体再設計計画](CORE_REDESIGN_PLAN.ja.md) を正本とします。
この TODO は個別の検討事項と予備調査を保持します。

クラス・主要動作モードの追加候補と、現行で対象外の機能の採否検討は
[追加機能の採否検討](FEATURE_CANDIDATES.ja.md) にまとめます。

## テスト計画のゼロベース再検討（2026-10-24、事前整理）

**目的は今後の比較に使う根拠と検討課題の準備。P4多配線での機能確認と、書込み・更新の
エコシステム確認を分けることを基本方針とし、具体的な構成、代表機、機材の購入、合格閾値は未決定。**
現行の [テスト計画](tests/TEST_PLAN.ja.md)・[実行手順](tests/README.ja.md)・
[カバレッジ](docs/test-coverage.ja.md) を調べ直し、pytest-embedded-arduino-cli の profile を
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
| 汎用系列の `opt` / `rtlib` | 各3 / 4選択肢 | clockも全積にすると1 sketchで `81×3×4=972` セル。pnum・route・controllerはまだ含まない |
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

現行の [profile生成器](tools/generate/sync_example_profiles.py) は通常7系列を選び、
[要件判定](tools/generate/sketch_requirements.py) は CLK EN マクロと ANY のFlash/RAM上限を使う。
これは最初のbuild絞り込みには使えるが、正確なpartの端子、公開API、USB role、配線の保証には足りない。
例としてX035の [variant](variants/CH32X035/pins_arduino.h) は型番7種に対して
I2C1既定routeを「2 of 7 parts」と記し、I2C1は6 route、SPI1は4 routeを持つ。
`Wire` があることと、手元のpackageで既定のSCL/SDAが使えることを分ける必要がある。

### 系列差分をどの単位で扱うか

候補となる一行の単位は `contract × series/part/board × build条件 × 配線 × 観測方法`。
silicon、package、core実装、board引出し、fixture能力を別欄にし、
必要条件の積集合から実行候補を求める。未確認の能力を false と同一視しない。
profileを省いた理由も残し、単なる記載漏れで全件skipして成功する状態を検出する案を検討する。

下表は27系列を12群に並べた**差分調査の索引**。12台で十分という主張や購入リストではない。
根拠は [周辺機能表](docs/peripheral-support.ja.md)、[USB controller一覧](docs/usb.ja.md)、
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
当コアの [OEP連携文書](docs/oep-workflow.ja.md) は旧設定仕様も含むため、採用中のclient/plugin/probeで
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
  実測と未確認事項を揃えて [テスト計画](tests/TEST_PLAN.ja.md) の改訂案を作る。
  方式の採否、代表機の確定、旧入口の廃止、実装順はその時点の検討に残す。

## コア全体の理想像・範囲・学びやすさの検討

**コンセプトと提供範囲は仮説。理想像を先に描き、実測と利用例で修正してから最終仕様と実装段階を決める。**
2026-10-19 の追加指示に基づく。ここでの段階は検討・検証の順序であり、狭い製品仕様を先に公開して
後から別の実行モデルへ拡張する計画ではない。既存の実装、API、対象外方針は新設計の制約としない。

P0 の成果物として [全体設計案 P0-1](planning/core-redesign/design-draft.ja.md)と
[利用例・状態遷移](planning/core-redesign/usage-sketches.ja.md)を作成済み。
明示 step、短い work、宛先 event の三案と D01〜D12 の比較軸を具体化した。
いずれも未採用で、以下の TODO や機能候補の採否は変更していない。
P1 で [比較契約](planning/core-redesign/experiment-spec.ja.md)、
[用途・刺激](planning/core-redesign/workloads.ja.md)、[実行順](planning/core-redesign/experiment-tasks.ja.md)を定義済み。
EXP-00の再現・記録と、[対象・host対照ロジックの準備](planning/core-redesign/p2-preparation.ja.md)を実施済み。
X0の共有bus・周期処理もhost対照へ結合済み。[P4までの実行状況](planning/core-redesign/execution-status.ja.md)に従い、
通常モデルで可能な部品・用途の比較を進める。
方式の採否は未決定。物理条件・実機・教材の未検証を分けて P2〜P4 で証拠を集める。

### 理想像のたたき台と検証したい仮説

「小型 CH32 で、短い処理と限られた資源を自分で組み立てられ、その使い方を学べる Arduino Core」を候補にする。
V003・V006・X035 を主対象の候補とし、V307・H417・後継への展開を全体像に含める。
他マイコン向けコードの無変更移植より CH32 向け最適化を優先しつつ、利用者が全ハードウェアを直接管理せずに
意味のある操作単位で組める範囲を探る。教育目的のためだけに不要な定型コードを増やさない。

- 必要な機能・エラー処理・連携まで直接実装するとコア相当の共通処理になる、という仮説を検証する。
  Arduino の名前や API 形状だけで費用を判断せず、共通化で減る重複と汎用化で増える費用を測る。
- ノンブロッキングな処理の分割、状態保持、通知、容量指定を利用者に見せることで、
  小型マイコンの制約を理解しながら実用的なアプリケーションを作れる、という仮説を検証する。
- キュー・リング等のまとまった部品と統一した操作規則が、利用者による個別実装よりも資源費用と理解の負担を
  下げられるかを検証する。行数の少なさや class 数の多さだけで良否を決めない。
- 実行器、イベント配信、待機補助などの便利さが、固定費・隠れた実行・理解すべき規則の増加に見合う範囲を探る。
  通常処理の時間保証を付けず、遅延後の意味、所有、失敗処理は定義する。

### 最終像で先に描く責務と拡張境界

以下は責務の候補であり、各行を独立した class や常駐サービスにする指定ではない。

| 領域 | 利用者に示す内容と共通化の候補 | 最終像で検証する拡張境界 |
|---|---|---|
| 起動・基本ランタイム・系列対応 | 最小起動、必要な初期化、資源使用の見通し | 系列・ボード・コア数の差をどこへ閉じ込めるか |
| GPIO・時間・周辺 I/O | 意味のある操作、非同期開始と結果、ハードウェア資源の競合 | 系列固有能力、転送方式、割り込み、時刻源を交換できる範囲 |
| データ・同期 | queue、ring、flag、容量と満杯時の結果 | 単独利用、ISR、複数 producer、コア間で規則が変わる部分 |
| 処理進行・通知 | 短い処理、再実行契機、timer、完了と失敗 | システムと利用者の処理、明示実行・loop 外・delay 中の実行範囲 |
| メモリ・永続化・省電力 | storage の容量・寿命、保存と復旧、停止中の扱い | 小容量向け構成、系列の物理条件、任意機能への依存 |
| ライブラリ・利用例・診断 | 部品の組み合わせ、状態遷移、資源費用の確認 | 外部ライブラリの参加規則と、コアに常設しない補助機能 |

拡張ポイントは、実行文脈、所有権、寿命、能力、エラー、依存方向の契約として先に設計する。
確保する手段は型・リンク時選択・個別 adapter・実行時 interface 等を比較する。
将来用の仮想関数、空の登録表、予約 RAM、汎用 dispatcher を常設することを必須としない。
代表的な拡張を試作で接続し、基礎 API や利用者の状態保持を作り直さず成立するかと、未使用時の費用を確認する。
未知の後継まで無条件の互換を約束せず、成立条件と対象外も最終仕様に含める。

### 学べる使い方を評価する

利用者が、サンプルを動かすだけでなく、変更した結果を説明・予測できることを評価候補にする。
教材の難易度を段階化しても、実行規則や API の意味は途中で変えない。

| 教材・変更課題の候補 | 学べる内容 | 確認する観点 |
|---|---|---|
| 入力を取りながら周期出力 | 処理を返す意味、時間と状態の分離 | 待つ場所と次に実行される契機を説明できるか |
| 通信を開始して完了を扱う | 開始と完了の違い、buffer の寿命 | 完了前の書き換えや破棄を避けられるか |
| 小容量 queue で burst を受ける | 有限容量、集約とデータ保持の違い | 容量を変えた RAM と loss の変化を予測できるか |
| 長い転送・計算を分割する | 1回の仕事量と他処理の進行 | 分割量を変えた応答と処理費用を説明できるか |
| 切断・満杯・失敗から復帰する | 状態遷移、cancel、再試行、所有 | 正常経路以外も新しい処理へ組み込めるか |
| X035 の PD・PPS・表示・入力を組む | 同じ規則で複数機能を連携する方法 | 通信状態と UI の状態を区別して追えるか |

各例に状態・容量・実行契機の小さな説明と、変更して観察する課題を付ける。
レジスタの逐次操作まで覚えることを入口の必須条件にはせず、必要なときにハードウェア側へ辿れる説明を比較する。
機械計測では Flash・RAM・stack・応答・loss を確認し、学びやすさは課題の完了、誤解、説明の必要量、
状態の追いやすさを利用者レビューで確認する。参加者・経験・課題・条件を記録し、未実施の教育効果を主張しない。
学びやすさと資源費用を別々に報告し、根拠のない単一スコアで採否を自動決定しない。

### 理想像から最終仕様へ進む順序

[全体計画のフェーズ](CORE_REDESIGN_PLAN.ja.md#フェーズと高位モデルを使う境界) に従う。
理想像と比較条件を高位モデルで設計し、定義した実験を別モデルへ渡す。
比較結果から最終仕様を選ぶ段階で高位モデルへ戻り、その後に同じ最終規則に従った本実装を進める。

比較では機能、容量、失敗処理、最適化条件を揃えた用途別の直接実装を基準の一つにする。
空のプログラムだけでなく、単機能と完成用途の総量・差分を測る。未使用費用と実使用費用を分ける。
許容費用・容量の予算は用途側が必要とする領域も見積もって候補を置き、結果に合わせた恣意的な合格判定を避ける。
試作で理想像を修正することは許容するが、製品 API の公開・固定を比較完了より先行させない。
教材案は仕様選定の入力にし、正式な examples と実装順は最終仕様に基づいて定める。
以降のタスク、永続化、および [機能候補](FEATURE_CANDIDATES.ja.md) は、この全体像の下で採否を検討する。

## 既存プロジェクトからの用途・比較実験候補

**2026-10-19 の予備調査。以下は候補であり、実装の採用や性能の優劣を決めたものではない。**
公開 README、対象ソース、ビルド定義、ライセンス表示を確認した。上流のプログラムはビルド・実機実行していない。
作者の性能値や消費電力は当コアでの測定値と区別し、ここでは合格基準として転記しない。
特定作者・表示機器を使う例に偏りがあるため、CH32 全体でどの方式が主流かの根拠にはしない。

### 実用途の候補

リンクは調査した commit に固定した。比較したい事項は当プロジェクト側の提案である。

| ID・参照 | 確認した用途・構成 | 比較に使いたい内容・注意点 |
|---|---|---|
| UC-01 [USB PD Adapter](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/tree/7eba86abd2467bb53f609189d8c3c7d75e4d6c00) | X035、PPS 操作、INA219 測定、SSD1306 表示、3ボタン。GPIO・時刻・software I2C・PD の各部品を同梱 | 最優先の完成用途。交渉中の入力・表示、要求集約、測定周期、切断。元のプログラムにはブロック待機があり、新方式との動作差を記録する |
| UC-02 [USB PD Tester](https://github.com/wagiminator/CH32X035-USB-PD-Tester/tree/d835a923228e349cddc444a188658a918d89d02b) | X035、電源能力の一覧と固定電圧／PPS の選択、OLED | UC-01 と共通の PD 部品を別 UI で使う例。同一機能を別アプリへ持ち出すときの変更範囲を評価 |
| UC-03 [I2C Knob](https://github.com/wagiminator/CH32V003-I2C-Knob/tree/5302daa8990ee850de18c963ade8968e4e976350) | V003、ロータリエンコーダを I2C slave のレジスタとして公開。通常処理と IRQ 間で値を共有 | 小規模な基準用途。入力の取りこぼし、複数 byte の値の一貫性、critical section、固定費。queue が本当に必要かも比較 |
| UC-04 [GameConsole](https://github.com/wagiminator/CH32V003-GameConsole/tree/ad10df94a7571768d658b03238379f99f0a3f061) | V003、ゲーム・入力・OLED。調べた tiny_conway は DMA 表示転送と次世代計算を重ねる | RAM の厳しい表示＋計算。DMA を使っても開始・完了で待つ箇所があるため、API 名だけで非同期と分類しない。ゲームごとに機能・素材・ライセンスを確認 |
| UC-05 [osillyscope](https://github.com/tvlad1234/osillyscope/tree/5ae18b98c4f67dcee8496ba370e7ab86f718614f) | V003、ADC・DMA・トリガ、OLED とボタン。ch32fun を submodule とし、取得・UI・表示を別ファイル化 | 取得 buffer と表示の寿命、IRQ から通常処理への受け渡し、取得欠落と描画負荷。表示器だけでなくアナログ前段も条件を揃える |
| UC-06 [Hydrogen](https://github.com/PhenixTech/Hydrogen/tree/1e83379c4a27fbec0d656e80dbda264f1606d8ea) | V006、外部 RTC・OLED・入力・電池計測・sleep。WCH SDK 系。SDL simulator が UI・状態・ドライバの一部を共有 | 省電力と再初期化、時間の扱い、状態機械、ハードウェアなしの教材。電池寿命の宣伝値を再現済みとはしない |
| UC-07 [OpenServoCore](https://github.com/OpenServoCore/open-servo-core/tree/98965080a20e8f692f2c488ac60876f395fb2192) | V006 のスマートサーボ向け Rust プロジェクト。README は通信を実機検証済み、制御等を開発中と区別。チップ非依存部・CH32 部・board を分離 | 通信、資源所有、機材依存部とロジックの分離の参考。完成したサーボ制御ベンチとは扱わず、通信部分を別途監査。言語・toolchain が異なるのでサイズを直接順位付けしない |
| UC-08 [ch32fun usbfs_i2c](https://github.com/cnlohr/ch32fun/tree/a800cbd1f1ca5e250c73da83c669f0965d2052e5/projects/usbfs_i2c) | hardware USB FS と software I2C のブリッジ。README は X03x 等を対象とする。ソースは fsusb と static_i2c を利用 | ホストからの要求、buffer、I2C 応答・失敗、backpressure の題材。対象系列の全組み合わせを実機確認済みとは扱わない |

候補は全部を実装するリストではない。まず UC-03 と UC-01 を小規模・完成用途の比較軸とし、
UC-05 を取得と buffer、UC-06 を sleep と教材の検証候補にする。これは実験の選び方の案であり採用確定ではない。
タッチ入力・永続化の電断復旧・複数コアの完成用途は、今回の調査で十分な材料が揃っていないため継続調査する。

### ソースから確認できた再利用と結合の境界

「各プロジェクトが全部を独自に再実装している」とは結論しない。次の事実が確認できた。

- UC-01／02 の `usbpd_sink.c` と `system.c` は、それぞれ調査 commit 間で SHA-256 が一致した。
  UC-01 の `millis.c` は [MCU-Templates](https://github.com/wagiminator/MCU-Templates/tree/7e24501219782a1d35b4223eaaf2478c98ac5e79)
  の `CH32X035/libraries/millis.c` とも一致した。共通部品をコピーして再利用する実例である。
  一方、UC-01／02 の `i2c_soft.c` は bit timing の補正定数が異なる。差の理由や性能は未検証であり、劣化や不具合とは断定しない。
- UC-01 の [main.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/main.c)
  は測定・入力・表示・電圧要求を順次呼ぶ。
  [usbpd_sink.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/usbpd_sink.c)
  の `PD_negotiate()` は待機しながら `PD_update()` を呼び、USB PD IRQ と状態を共有する。
  [millis.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/millis.c)
  は SysTick とその handler を設定する。関数が分かれていても、他のサービスと合成するときは待機・IRQ・時間資源の規則が必要になる。
- UC-03 の `main.c` は、I2C の変更・busy 状態を見て、割り込み禁止区間でレジスタ値を読み書きする。
  UC-04 の `tiny_conway/src/i2c_dma.c` は DMA を使う一方でバス状態待ちを含む。
  「割り込みを使う」「DMA を使う」だけでは、呼び出し側の待機や他処理への影響までは分からない。
- UC-05 は既存の共通基盤 ch32fun を取り込み、UC-06 の `software/sim/Makefile` は製品の UI 等を simulator と共有する。
  UC-07 も非依存ロジックとハードウェア側の分離を明示している。再利用・テストのための境界を持つ例も比較対象に含める。

以上からの作業仮説は「再利用する部品は存在するが、待機・時間源・IRQ・buffer・容量の規則を揃えないと、
異なる用途の部品を組み合わせにくい」。組み込みの変更作業をまだ実施していないため、困難さは定量化していない。
当コアの価値を検証する際は、部品を別アプリへ移す、同じ I2C バスへ別機器を追加する、入力を変更する等の
課題を与え、ハードウェア以外へ波及する変更、必要な知識、資源競合、機能を足したときのサイズ増分を記録する。
基盤に依存すること自体を悪い設計とは扱わず、用途内で有効な最適化と他用途への移植費用を分ける。

### 共通基盤・計測の参考

| 参照 | 使う目的 | 比較の限界 |
|---|---|---|
| [MCU-Templates](https://github.com/wagiminator/MCU-Templates/tree/7e24501219782a1d35b4223eaaf2478c98ac5e79) | startup・linker・GPIO・時間・通信等をまとめる小さな基盤の実例 | ライブラリの存在と統一された非同期実行規則の有無を分けて調べる |
| [ch32fun](https://github.com/cnlohr/ch32fun/tree/a800cbd1f1ca5e250c73da83c669f0965d2052e5) | 直接操作を支える共通基盤と examples。機能を揃えた比較の候補 | 「基盤なし」の対照群とは呼ばない。README の小さな blink の値だけで完成用途を評価しない |
| [CoreMark port](https://github.com/cnlohr/ch32fun/tree/a800cbd1f1ca5e250c73da83c669f0965d2052e5/projects/coremark) | compiler・最適化・Flash／RAM 実行等の演算条件を把握する補助 | I/O の合成や使いやすさのベンチではない。既定の data 領域だけでも2000 Bを使うため V003 への適合を別途判断。縮小実験と標準条件のスコアを混同しない |
| [stack_usage](https://github.com/cnlohr/ch32fun/tree/a800cbd1f1ca5e250c73da83c669f0965d2052e5/projects/stack_usage) | canary による使用範囲の観測と backtrace の教材 | linker、heap、稼働中 stack、割り込み、frame pointer 等の前提を確認する。観測した書き込み範囲を全経路の最大必要量の証明とはしない |

### ベンチマークにするために揃える条件

- 上流そのものの再現、同じ観測可能な機能を新規則で実装する比較、故障処理等を追加した比較を分ける。
  同じ機能での比較値と、追加した保証・機能の費用を別々に報告する。
- compiler・flags・LTO・clock・実行領域・linker・デバッグ出力・初期化範囲を記録する。
  上流の想定環境での再現と、可能な範囲で条件を揃えた比較の両方を扱う。付属 binary の容量をそのまま当コアと比較しない。
- 表示解像度・font・描画範囲・bus 速度・入力列・測定頻度・buffer 容量・接続相手を揃える。
  I/O trace や仮想入力を保存し、実機とロジック単体の試験の責任を分ける。
- 元実装で待機や停止する場面を調べる。新方式で非同期化・復帰を加えたときは、その動作差を明示する。
  軽さに加え、入力応答、他処理の進行、loss、失敗・再試行、最大 stack、資源追加時の増分を測る。
- 組み合わせる範囲は「空」「単一部品」「2種類の独立した部品」「完成用途」とする。
  サイズの足し算だけでなく、共通化による共有と、結合に必要な状態・buffer の増分を確認する。
- 上流の make 既定 target が書き込みを含む例もあるため、今後の再現ビルドでは build 専用 target を確認する。
  実機実行は別途特定した fixture と測定条件で行う。今回、書き込み・上流スクリプトの実行はしていない。

### ライセンス表示と調査の追跡

コードの同梱・翻訳・改変の判断と、ユースケースの比較材料として参照することを分ける。
今回確認した上位の表示は、wagiminator の上記各 repository が CC BY-SA 3.0、osillyscope／ch32fun が MIT、
Hydrogen が GPLv3 と WCH 由来ファイルの例外、OpenServoCore の firmware が MIT OR Apache-2.0、hardware が CERN-OHL-P v2.0。
これはファイル単位の権利確認を完了したという意味ではない。実際の取り込み候補では header、submodule、
font・画像・外部ソースの由来まで確認する。現時点で製品ソースへの取り込みはしていない。

各 repository の固定 commit、tree、確認した README・LICENSE・主要ソースは
`work/usecase-research-20261019/` に保存した（Git 管理外）。固定 URL が再調査の入口になる。
再利用性の結論には、ソース確認に加え、ビルド再現・組み合わせ課題・実機測定が必要である。

## タスク・処理全体・共通ランタイムのゼロベース再検討

**方式・公開 API・構成は未決定。2026-10-19 の追加指示に基づき、検討範囲と前提を更新した。**
小容量 CH32 への最適化、利用者もノンブロッキングに処理を分割する構成、キュー・リングバッファの提供を
希望する方向として評価する。上記のコア全体のコンセプトとともに、教育面・資源費用から適切な範囲を検証する。
共通機能をどこまで用意するか、ブロック待機をどこまで扱うかは実測と利用例で判断する。
既存コード、既存 API の挙動、ADR の選択、移行の容易さを新方式の制約・採否の根拠にしない。
コアとライブラリは、決定した新しい規則に合わせて組み替える。
従前の現行実装棚卸しと、それを前提とした方式比較は、この項の判断材料から外す。
関連する機能候補は [追加機能の採否検討](FEATURE_CANDIDATES.ja.md) にあるが、その分類・対象外方針も固定条件ではない。

通常処理の実行開始・完了時刻は保証しない。長い処理では利用者が delay(0) 等で処理機会を渡す構成を含め、
遅延を許容して処理を進める仕組みを検討する。遅延を理由とした強制 reset を標準の解決策にはしない。
割り込みやハードウェアで進む処理と、通常処理へ戻ったときに進む処理を区別する。
WDT の有無・用途は別項目とし、時間保証やタスク切り替えの代用にしない。

V003・V006・X035 を設計の中心にし、H417・後継までのサイズ・速度・使いやすさ・保守性も評価する。
ESP32 に似た機能のまとまりは参考にするが、特定 OS、スタック構成、イベント基盤の再現は前提にしない。
**共通化する範囲と利用者への公開範囲を含む最終像を比較し、実験で仕様を固めた後に実装段階を分ける。**
以下は仕様候補と判断事項であり、採用決定ではない。

### 時間保証をしない場合にも決める動作

- 処理が遅れた場合は、後からそのまま実行、通知を集約、古い要求を失効、明示的に失敗を返す等を用途別に比較する。
  時間保証をしないことと、イベント順序・データ所有・満杯時の結果を未定義にすることを区別する。
- delay(0) は処理機会を一度提供する、有限件数まで進める、明示的な実行関数の別名にする等を比較する。
  「待機時間が0」でも関数全体の所要時間が0とはしない。空になるまで処理する案は、連続投入で戻らない場合も評価する。
- delay(n) は待機中にどの処理を進めるか、callback 内ではどうするか、終了予定時刻を超えた処理をどう扱うかを決める。
  delayMicroseconds、setup 中、ISR、割り込み禁止区間についても、動作・許可・エラーを明確にする。
- callback が戻らない／処理機会を渡さない場合、同一コアの協調実行は他の通常処理を進められない。
  戻った後に遅延を記録する、IRQ 側で異常フラグを立てる、任意の WDT で復旧する等は別機能として比較する。
  任意の C/C++ 関数を安全に中断・破棄して次へ進む機能を、軽量な timeout として暗黙に導入しない。
- WDT は任意の停止検出・復旧機能として、利用者による feed、全体の進捗による feed、
  診断だけ行う構成を比較する。delay(0) や IRQ が走るだけで feed すると、目的の処理が停止していても
  検出できない場合がある。遅い処理と停止をどう区別するか、再起動後の状態をどう扱うかは用途側の判断も含める。
- 波形生成・バス応答等の外部条件はハードウェア／IRQ の設計条件として扱う。
  通常処理が遅れて通信が失敗する場合の報告・再試行を定義するが、共通基盤による期限保証は要求しない。

### 仕組みを一括してタスクと呼ばずに分ける

| 機能 | 表すもの | 共通基盤へ載せるかを決める観点 |
|---|---|---|
| 処理予約／work | 指定した処理を、実行機会が来たら呼ぶ | 同一処理への重複要求を一つにまとめるか。宛先が既知なら event type や購読表が必要か |
| メッセージ／データ queue | 順序を持つ複数のデータを渡す | 各データを保持する必要、満杯時の結果、コピー／参照／所有移転 |
| signal／event flag／counter | 条件が変わったこと、発生回数 | 値は別 buffer に置けるか。集約してよいか。消費と再通知の競合 |
| 処理完了通知 | ある操作の成功・失敗・cancel が確定したこと | 操作との対応、結果の寿命、通知が満杯で失われたときの回収手段 |
| 宛先付きイベント配達 | 特定の object／handler に出来事を届ける | 直接 callback を予約する形と、型付き event を queue に積む形の費用・使い勝手 |
| 購読型イベント配信 | 一つの出来事を複数の購読者へ伝える | event ID、購読表、配信順、解除、fan-out、payload の寿命。全部の通知に必要か |
| timer／遅延処理 | 指定時刻以降に処理や通知を実行可能にする | work と一体か別部品か、期限走査、周期遅延の扱い、未使用時の費用 |
| I/O 操作管理 | 開始から完了・失敗までの状態を進める | 状態機械を各ドライバが持つか、共通の operation／request を持つか |
| 実行器／event loop | 実行可能な仕事を選び、呼び出す | どの queue／timer／work を共有するか。単一／複数 instance、明示／自動実行 |
| 同期・資源管理 | データ更新・数・周辺資源の所有を調停する | scheduler から独立利用できる範囲、ISR／同一コア／コア間の保証 |

例えば「UART buffer に新しいデータがある」は、buffer＋一つの集約通知で表せる場合がある。
各 byte を汎用 event としてコピーする案、操作完了を一対一で通知する案、接続状態を複数購読者へ配る案は、
必要な機能が違う。全経路を一つのイベントバスへ統一するかは比較結果で判断する。

### 共通化・公開・自動実行を別々に選択する

| 選択軸 | 比較する候補 |
|---|---|
| 実装の共通化 | 各ドライバで保持、コア内の共通部品、コアと外部 library が使う共通部品 |
| 利用者への公開 | 非公開、システム通知の購読だけ公開、宛先付き work／event の投入も公開、独自 loop／channel の作成も公開 |
| 自動実行への参加 | システム専用、明示登録したユーザー処理も参加、ユーザーは明示実行専用、用途により選択 |
| 実行器の数 | 全体で一つ、システム／ユーザーで別 instance、コアごと、利用者が複数作成 |
| 容量の共有 | 全体 pool、用途別予約枠、instance ごとの固定容量、利用者が buffer を渡す |
| 配信の形式 | 直接 callback、宛先付き message、購読型 publish／subscribe、これらの任意の組み合わせ |
| 選択方法 | ビルド時の機能選択、使用 object からリンク、型／template の選択、起動時設定 |

同じ基盤を公開しても、内部システム queue へ自由に投入させる必要はない。
システム event の購読を許可することと、システム event の発行を許可することも別の権限として考える。
内部から登録された処理というだけで「短い」「再入可能」「ユーザーコードを呼ばない」と仮定しない。

同一 stack 上に複数の loop／queue を作っても、それだけでは時間的に独立しない。
分割によって得られるのは容量、実行対象、所有、順序、ライフサイクルの分離であり、
一つの長い callback が他の処理を待たせることは別途扱う。

### 比較する最終構成の例

次の並びは実装の段階ではなく、別々の最終構成候補である。組み合わせも比較する。

| 構成候補 | 共通で提供するもの | 利用者に見えるもの | 主な比較点 |
|---|---|---|---|
| 個別処理＋共通の同期部品 | queue、通知、時間・資源操作 | 各機能の poll／callback | 全体の規則を揃える費用、利用者の記述量、各機能の重複実装 |
| システム専用の処理基盤 | 内部 work／timer／通知、実行機会の管理 | ドライバ API と個別 callback | 小さな公開面と、外部 library が独自基盤を必要とする費用 |
| 公開する軽量 work 基盤 | 短い処理予約、集約通知、遅延 work、同期 | 同じ規則のユーザー work と任意の queue | event ID／購読表を持たない構成の表現力と制約 |
| 公開する event loop 基盤 | 宛先付き event、handler、timer、loop | ユーザー定義 event・loop。自動実行参加は別選択 | 型・payload・順序・寿命の規則、配達費用、容量と状態の増加 |
| 公開する購読型配信も含む基盤 | event loop に加えて購読・分類・複数宛先配信 | システム event 購読とユーザー定義 channel | コンポーネント間の疎結合、fan-out と購読管理の費用、未使用時に独立して除去できるか |

状態機械、スタックレスな記述補助、非同期結果 object、await に相当する記述、独立 task／RTOS は、
上記に何を加えるかとして比較する。使いやすさのための記述補助と、stack／実行モデルの変更を分ける。
専用 stack の不要な基本構成を評価しつつ、必要なら外部の別実行モデルから同じ部品を使える境界も検討する。
全方式を同梱すること自体を目的にはしない。

参考として [ESP-IDF Event Loop](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_event.html)
は default／user loop を持ち、専用 task を作らない設定もある。これは「イベント基盤の存在」と
「イベント基盤が独立 task を持つこと」を分ける比較材料になる。FreeRTOS 依存をそのまま移す提案ではない。
[Zephyr workqueue](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html) は、
重複投入の集約、再投入、遅延 work、cancel と寿命の仕様の比較資料とする。
その worker thread を採用する意味ではない。

### 実行機会・種類・深さ制限の規則

- loop の前後、delay(0)、delay(n)、専用の実行関数、IRQ 復帰、別コアをそれぞれ評価する。
  利用者が長い処理を続けたい場合は適度に実行機会を渡す使い方を含めるが、その間隔を全用途に固定しない。
- 実行対象を「内部状態の進行」「ユーザーへの配達」「任意の housekeeping」等に分けるか、
  単一種類の work に実行可能な場所の属性を持たせるかを比較する。
  名前としての優先度と、呼び出してよい文脈を混同しない。
- 一巡の最大件数、snapshot した ready 集合、round-robin、優先順、重み付き配分、処理間の時間予算を比較する。
  選択基準は公平性・loop へ戻る性質・費用であり、期限保証ではない。
- callback 中の delay(0) を無処理にする、後続処理を次回へ保留、内部処理だけ許可、
  異なる work の限定的な入れ子を許可する等を比較する。同一 work の再実行、別 work 間の循環も扱う。
- 実行器の深さ、callback 連鎖、IRQ の入れ子、ユーザー関数自身の深さ、queue 容量を別々に扱う。
  深さ上限で抑制された仕事を消してしまわないこと、停止理由と再開条件を記述する。
- work A が資源を保持し delay(0) を呼び、work B がその資源を待つ例を比較に含める。
  待機側を即時失敗・再通知へ変えるか、所有中は該当する配達を止めるか等の契約が必要で、
  深さを2に制限するだけで成立したとはしない。
- setup 前後、サービスの初期化途中、begin／end、callback 中の登録解除、自己再登録、cancel、
  object の破棄、reset／sleep 復帰のライフサイクルを定義する。隠れた constructor 登録も選択肢の一つとして比較する。

### イベントのデータ・順序・配送保証

- payload を固定長コピー、可変長コピー、借用参照、所有移転、共有 pool のどれで扱うか。
  callback の引数がいつまで有効か、複数購読者への配達で誰が解放するか、C／C++ の API をどう揃えるかを検討する。
- event の種類と宛先を、関数＋context、数値 ID、型、handle 等で表す案を比較する。
  名前空間、handler の登録数、event の大きさを全系列に固定しない。
- queue 内 FIFO、producer ごとの順序、異なる queue 間、同時刻 timer、コア間の順序を分ける。
  全体で一つの厳密な順序を要求するかも未決定とする。
- 受理失敗、受理後の cancel、購読者の解除、未登録宛先、遅延して無意味になった event をどう扱うか。
  少なくとも成功戻り値の意味を、格納できた・配達済み・処理完了のどれかとして明確にする。
- 満杯時の失敗・新しい通知の破棄・古い通知の上書き・同種通知の集約を用途ごとに比較する。
  遅延の許容は、有限 RAM に無限に蓄積できることを意味しない。発生回数を保持する通知も別に検討する。
- 実行完了／失敗の通知を失うと buffer や資源を回収できない設計では、予約枠、状態照会、再通知等を比較する。
  機能によって必要な配送の保証を分け、全部を高価な一律の配送方式にしない。
- 一つの event の配信中に購読者が増減する場合、全購読者を一度で呼ぶか分割する場合、
  最後の購読者を待つ間の payload 保持量を測る。
- ISR から投稿できる API と、直接 ISR callback を呼ぶ API を分ける。
  通常の通知 API が同期実行されるか遅延配達されるかを、呼び出し元の条件で不意に変えない契約を比較する。

### 時間・I/O・同期・省電力を含む共通基盤の範囲

- 時間源と timer 配達を分ける。timer 満了は実行可能になったこととし、遅れた周期を一度にまとめる・
  回数で通知する・追い付く・次回基準をずらす等を比較する。wrap、再設定、cancel、停止中の時間も扱う。
- I/O は開始・状態確認・結果取得・完了 callback・cancel のどこまでを共通化するか。
  各機能が独自の状態を持つ案と、共通 request／operation を使う案を比較する。
  同期 wrapper を提供する場合も、待機中に何を進めるか、callback から呼べるかを新規則で定める。
- キュー／リングバッファは利用者に公開する部品として設計する。flag／counter、binary／counting semaphore 相当、
  critical section、所有権付き try-lock、buffer pool の追加範囲を比較する。
  これらを使うだけで event loop や timer がリンクされる構造を必須にはしない。
- 同一コアの通常処理、ISR、複数 producer／consumer、コア間を分けて安全性を定義する。
  実行中の callback では待たずに失敗・再試行する案と、待機可能な別文脈だけで blocking を提供する案を比較する。
  「FreeRTOS 的な一式」を、待機 task・priority inheritance を含む API 完全互換とは決めない。
- 周辺資源の owner を object／request／core で表すか、複数要求を queue で直列化するか。
  Mutex を用意しただけで複数ステップの transaction 全体が安全になるとは扱わない。
- 仕事が無いときの busy poll／省電力待機、timer・IRQ・コア間通知による再開を比較する。
  idle 検出と sleep の間に通知を見落とす競合も評価する。WDT と省電力の方針は独立して選択する。
- 診断は残件数、overflow、再入抑制、最後の進捗、観測した処理時間等を候補とする。
  機能ごとに費用を計上し、診断を使わない構成にも統計領域を必須にしない。

### 利用者向けキュー・リングバッファと待機の境界

利用者向けに提供したい機能として評価する。提供範囲、クラス名、型、内部構造、容量指定、同期方式、待機 API は未決定とする。
FreeRTOS 的な部品の充実と、独立 task の停止・再開を再現することを分けて設計する。

- 型付き固定長メッセージの queue と、byte 列／要素列の ring buffer を比較対象にする。
  enqueue／dequeue、peek、使用量・空き容量、reset、満杯・空の結果、部分転送を整理する。
  コピー可能な型の制約、参照を渡す場合の所有・寿命、処理中の reset／破棄の可否も決める。
- 固定容量 template、利用者提供 storage、容量を実行時に指定する方式を比較する。
  バッファ本体に加え、管理領域、アラインメント、1 instance ごとの費用を表示する。
  未使用、1個、複数個、同じ型／異なる型でのコード共有を測る。
- 即時に成否を返す操作を比較の基準とし、空き／データ到着の通知は独立に選べる構造を評価する。
  通知を使わない queue に実行器、timer、購読表を必須にしない。
  状態確認から通知登録までの間に条件が成立する競合と、通知後に別の consumer が消費する場合を扱う。
- 単一 producer／consumer、複数 producer／consumer、通常処理と ISR、コア間で必要な同期を区別する。
  ring という構造名だけで ISR 安全・コア間安全とはしない。割り込み禁止時間とコピー量も測る。
  参考として [FreeRTOS Stream Buffer](https://freertos.org/Documentation/02-Kernel/04-API-references/08-Stream-buffers/03-xStreamBufferSend)
  にも単一 writer／reader の前提があり、各部品の利用条件を明示する比較材料とする。

| 待機の候補 | 比較する規則と費用 |
|---|---|
| 即時成功／失敗 | 呼び出し元が状態を保持して戻る。次回の再試行、通知との組み合わせ、記述量 |
| 非同期の期限付き要求 | 関数は戻り、成功・期限切れ・cancel を後で取得。待機登録の RAM、結果と通知の競合、寿命 |
| 他の通常処理を呼ばずに待つ | 通常処理が停止する。IRQ／DMA／別コア等だけで条件が成立するか、時間源が進むか、禁止文脈 |
| 待機中に共通処理を呼ぶ | 呼び出し元の stack と資源を保持する。再入、循環待ち、深さ、配達対象、元の呼び出しへ戻れる条件 |

同じコアの producer を待って consumer が戻らない例、資源を保持した処理が別 callback を呼び出す例を試験する。
timeout を付けても producer が進めない構造は解消しない。非同期の timeout 判定が遅れる場合の結果の規則も決める。
ISR 内のブロック待機、無期限待機、callback 内待機を提供するかは明示的な採否項目とする。
データ格納部品と待機 wrapper を分離する案を比較し、待機を使わない構成の費用を独立に測る。

### 代表ユースケース：X035 の USB PD シンクと PPS 操作画面

USB PD シンクとして動作し、I2C 接続の画面へ状態を表示し、ボタンまたはタッチパネルで
PPS の設定を操作する完成例を、共通機能の採否と使い方の評価に用いる。
画面・入力機器用の汎用ドライバをコアへ取り込む判断とは分ける。
既存 API は前提にせず、新規則による仮想 I/O で候補を比較した後、機材と条件を定めて実機で測る。

| 処理 | 分割とデータ受け渡しの比較内容 |
|---|---|
| PD 接続・交渉・維持・切断 | ハードウェア／IRQ と通常処理の分担、進行状態、完了と失敗の回収、接続世代による古い結果の無効化 |
| ボタン／タッチ入力 | 入力取得、debounce、押下・解放・連続操作。保持すべき操作と最新値へ集約できる入力の区別 |
| PPS 要求管理 | 利用可能な条件と要求値の照合、編集中・要求中・成立済みの値の分離、連続変更の集約、失敗・再試行・切断 |
| I2C バスと表示 | 差分描画と転送分割、転送 buffer の寿命、入力機器とのバス共有、途中失敗・timeout・cancel |
| UI の状態反映 | 設定要求と交渉結果を区別した表示、変更通知の集約、古い描画要求の扱い |

通常処理の時刻を保証しない方針と、PD／PPS のプロトコルに従う条件を混同しない。
必要な応答・維持処理の条件を仕様から調査し、IRQ／ハードウェアへの分担と実測で成立範囲を確認する。
条件を満たせない場合は通信失敗や再接続等として扱い、成功を表示し続ける設計にしない。
具体的な規格値、機器、表示解像度、バス速度、入力方式は未決定で、実験条件として記録する。

- 直結した状態機械、work＋queue、宛先付き event、購読配信で同じ操作シナリオを記述する。
  全機能を共通 event に載せる案と、最新状態＋変更通知＋必要な FIFO だけを持つ案を比較する。
- 高頻度入力、表示更新中の PD 通知、タッチと表示の I2C 競合、切断・再接続、相手の失敗、
  queue 満杯、長い利用者処理、cancel と完了の競合を同じ条件で与える。
- Flash、静的 RAM、heap、最大 stack、queue 最大使用量、集約／破棄件数、IRQ 禁止時間、
  入力から要求・成立・画面反映までの各遅延、PD の失敗・再試行を記録する。
  バッファ容量や描画分割量を変え、通信の進行と UI の応答に対する効果を比較する。
- 完成例には処理ごとの状態・再実行契機・所有 buffer・失敗時の遷移を示す。
  API の単発例に加え、利用者がどこで返り、どこで処理機会を渡すかを示す教材にする。
  X035 の完成例と V003 の小規模な入力＋通信例を両方評価し、大きい例だけで共通基盤の固定費を決めない。

### V003・H417・後継への展開と未使用費用

- 全系列同一の仕組み、共通 API と複数 backend、規模別の機能集合、コア別の実行器を比較する。
  公開規則を共通にすることと、単一コア・複数コアに同じロック／queue／時間源を使うことを分ける。
- H417 では一方だけ使用、役割固定の両コア、コアごとに event loop、任意のコア間 message、
  共有 event loop を比較する。callback の実行先を固定するか、処理を移動できるかも未決定とする。
- 通常処理用 stack はコアごとに必要になることと、work ごとの stack を確保することを区別する。
  core 数を増やしても全 object に不要な core ID・lock・共有領域を強制しない構成を比較する。
- IRQ マスク、atomic、HSEM、memory ordering、cache／DMA、共有 SRAM と局所 memory、
  コア別 clock、起動・停止を backend の能力として評価する。未知の後継に対する無条件の互換を主張しない。
- 「未使用」を header 参照のみ、未登録、登録後解除、機能無効ビルドに分ける。
  非使用時の Flash／RAM／起動処理／実行時分岐と、使った分の最小固定費・1 object 当たり費用を記録する。
- 登録表・queue・購読表を静的に定義する案、利用者が領域を渡す案、pool、動的確保を比較する。
  空の購読基盤や timer の領域を全利用者へ常設しない条件を調べる。
- 共通化によるコード共有と、汎用化による metadata・間接呼び出しの増加、template 特化によるコード重複を測る。
  ビルド時選択、リンク時到達性、C++ 初期化、利用者が指定する容量を別々に評価する。

### 新しい比較実験の目的と順序

既存のドライバや既存 API の維持・移行コストでは比較しない。
比較用の小さな仮想 I/O と同じ workload で最終構成候補を評価し、成立した規則に合わせて実ドライバを新構成で実装する。
実験用の隔離した試作と、製品コードを段階的に積み上げることを区別する。

| 実験 | 同じ条件で比較するもの | 判断する事項 |
|---|---|---|
| 公開面と利用例 | システム内だけ、システム通知の購読、ユーザー work、ユーザー event loop、購読配信で同じ用途を記述 | 共通化の範囲、公開範囲、利用者・外部 library の記述量、必要な状態と寿命 |
| 動作モデル | 重複通知、順序、cancel／破棄、再入、自己投稿、満杯、timer の遅延、資源取得失敗 | 配送・所有・再入・遅延後の意味。時刻保証を判定基準にしない |
| サイズ・容量 | 使わない構成、queue だけ、work だけ、timer、購読1／複数、単一／複数実行器 | Flash、静的 RAM、heap、stack、登録と payload の費用、未使用時ゼロの成立条件 |
| 協調実行の負荷 | delay(0) を呼ぶ間隔を変え、burst、自己再投稿、長い callback、処理件数制限を与える | 応答の分布、残件数、loss、実行器から戻る性質、公平性、推奨使用例 |
| 新規則の I/O | 同じ規則で実装した受信・送信・timer・完了通知を組み合わせる | 通知集約や配信の必要性、同期 wrapper の扱い、過負荷時の動作 |
| 複数コア | コア別／共有 queue、通知、片側遅延・停止、共有 payload と資源 | 公開保証の共通部分、コア間 backend、容量・所有の分割 |

実験の用途例は、周期センサー＋UART、通信完了の連鎖、接続状態を複数機能が購読、burst 受信、
長い計算中の delay(0)、ユーザー定義 protocol、コア間データ処理などとする。
V003、X035、中容量・大容量系列、H417 を代表にし、利用できる実機・測定条件を明示する。

平均・分位・観測最大の応答、処理能力、loss、queue 使用量、最大 stack を測り、
性能の傾向と使用上の目安を出す。観測値を実行時刻の保証や強制 reset の閾値へ自動的に変換しない。
通常処理の遅延を許容しても、memory 破壊、二重完了、解放後の callback、通知の意図しない消失等は不成立として扱う。
compiler、flags、ソース、入力、機材、測定回数と、計測自体の費用を結果とともに残す。

公開面・動作の意味・容量費用の比較を経て最終構成を選び、その後に API 詳細、既定値・上限、
実装段階と examples を定める。公開する event 基盤を将来追加する前提で狭い仕様だけを先に固定しない。

### 独立したリンク費用の参考実験（実施済み、実機実行なし）

[比較スクリプト](tests/platform/probe_execution_link_cost.py)と
[実行方法](tests/platform/README.ja.md)を追加した。xPack GCC 14.3.0-1、`-Os`、function／data sections、
`--gc-sections`、LTO なしで、独立した最小プログラムを RV32E／ILP32E と RV32IMAC／ILP32 の2構成でリンクした。
例外情報も無効化しており、実 core の CRT・time・USB 等は含まない。下表は baseline との差分バイト数である。

| ケース | RV32E Flash差 | RV32IMAC Flash差 | 静的RAM差（両構成） |
|---|---:|---:|---:|
| 呼び出しを無効化、未使用 object を直接リンク | 0 | 0 | 0 |
| 呼び出しを無効化、未使用処理を archive に格納 | 0 | 0 | 0 |
| 未定義 weak hook を毎巡確認 | +14 | +16 | 0 |
| weak hook の定義を archive に入れただけ | +14 | +16 | 0 |
| 空の hook 定義を直接リンク | +10 | +12 | 0 |
| 使用する処理を strong 参照して archive から抽出 | +46 | +46 | +68 |
| constructor 登録の object を直接リンク | +64 | +64 | +72 |

baseline は Flash 24 B・静的 RAM 4 B。実 core のサイズを示す値ではない。
「使用する処理」は64 B buffer と4 B counter を操作する観測用の関数で、queue や dispatcher の実装ではない。
constructor のケースは callback pointer の4 Bも含み、`.init_array` を保持したときの到達性を検査している。
Flash は binary の配置の隙間を含む長さ、静的 RAM は `.data`＋`.bss`。stack、CPU cycle、実機遅延は未測定。

逆アセンブルと symbol も確認した。未使用 object は除去された一方、未定義 weak hook は呼び出し側の
判定・分岐が残る。weak 参照だけでは archive 内の定義を抽出しない点は、登録機構の成立条件にも関わる。
自動登録のために対象 object をリンクへ引き込む設計と、未使用なら消す設計を同じものとして扱わない。
実 core で厳密なゼロ費用を主張するには、未使用時に呼び出し経路まで消える構成選択と実際の ELF 比較が必要である。

2026-10-19 のローカル結果は `work/execution-link-cost-20261019-v2/result.json` と同ディレクトリの map／
symbol／逆アセンブル／全コマンドに保存した（Git 管理外）。再実行は未作成の出力先を指定する。
今回のビルド実験から、実行モデルの優劣や H417 対応を結論付けない。

## 永続化の調査と構成検討

**2026-10-22: フラッシュの raw アクセスの数字を集めた（docs/flash-raw.ja.md）。** 消去・書き込みの時間、処理中に CPU と割り込みが
止まるか、消さずに書き直せるか、途中で止めたとき・電源断のときに残る中身、書き込みのツールが末尾を消すか、リンカーで末尾を確保する形。
見つかった不具合（未修正）: `.highcode` が V003 以外では RAM に写されない（crt0 の CH32RV_HIGHCODE）。V003 で 64 バイトを超える printf の
行が黙って捨てられる。

**2026-10-22: 仕様案を書いた（docs/persistence-spec.ja.md、未採用）。** 3 つの層（raw の関数、末尾の領域、2 面の EEPROM）、系列ごとの単位・
確保・commit の時間の見積もり、比べた案、決めてほしいこと、実装の前に測ること。

**未決定。実装の採否、公開 API、層構成、保存方式、予約容量、保護範囲、既定値のいずれも決めていない。**
以下は今後の調査・比較事項であり、Preferences の採用、RAW とキー保存の2層化、Flash 末尾への配置、
メニューによる容量選択などはすべて候補として扱う。
関連項目は [追加機能の採否検討](FEATURE_CANDIDATES.ja.md) の SYS-01／02、STO-01〜04、UPD-01／02。

CH32RV には V003（Flash 16 KiB／RAM 2 KiB）など小容量の系列が多い。
ESP32 の API の使い勝手は参考になるが、同じ保存エンジン、最小予約量、RAM キャッシュ、
ファイルシステム、RTOS、保護機能を前提にしない。保存データの領域だけでなく、実装コード・定数・
作業 RAM・スタックを含む総費用と、アプリに残る容量で判断する。
**1セクタだけ予約する構成も比較対象に含め、二重化や複数セクタを一律の必須条件にしない。**

### 利用用途と要求を調べる

- 数バイトの校正値、固定形式の設定一つ、複数ライブラリの独立した設定、文字列・バイト列、頻繁に変わるカウンタ等を
  分けて、必要容量・更新頻度・読み出し頻度・許容停止時間を整理する。
- 電源断をまたぐ保存、通常 reset／低消費電力からの復帰、スケッチ再書き込み後の保持を別の要求として扱う。
  バックアップレジスタ・保持 RAM は電源条件が異なるため、Flash と同じ永続性を前提にしない。
- 全系列で同じ API を提供するか、小容量向けの機能制限・利用不可の構成を認めるかを検討する。
  小容量品で扱う最低限の用途を決める前に、最大容量・最大キー数・文字列長を固定しない。
- ファイル保存、ログ、ファームウェア自己更新との境界を整理する。設定保存だけで FS や更新機構まで
  必須にならない構成も比較する。必要に応じ、外付け EEPROM／Flash／FRAM 等へ任せる範囲も検討する。

### 系列と型番ごとの保存媒体の制約を調べる

- reference manual、datasheet、errata、EVT、`ch32-device-data` を照合し、仕様値・実機確認値・未確認事項を分ける。
  Flash 容量が同じでも同一ドライバ・同一単位を使えるとは限らない。必要な事実を device-data に持たせる範囲を調べる。
- 読み出し単位、通常書き込みの最小単位、アラインメント、高速／ページ書き込み単位、消去単位・境界を別々に調べる。
  「ページ」「セクタ」「ブロック」という名称だけで比較せず、各操作の実バイト数を記録する。
- 消去方式が複数ある場合の粒度・時間・制約、同じ書き込み単位への再書き込み可否・回数制限、ビットの変更方向、
  ECC 等による追加制約を確認する。消去せず追記できることや、最後に有効ビットだけを書けることを仮定しない。
- 保存先として利用できる領域を調べる。ユーザー Flash、専用 data Flash／EEPROM の有無、予約領域、bootloader、
  option bytes・工場情報、bank 分割、容量・配置を変える設定を確認する。
- リンク用アドレス、読み出し用のメモリマップ、Flash 制御に指定するアドレス・alias の関係を調べる。
  `pnum=ANY` で保証できる容量・操作と、実チップの容量検出の可否・信頼性も整理する。
- 消去・書き込みの寿命、データ保持条件、電圧・クロック条件、所要時間、途中失敗時に影響し得る範囲を調べる。
  原子的に扱える最小操作があるか、単なるソフトウェア上の完了と実際の書き込み完了を区別できるか確認する。
- Flash 操作中に同じ bank のコードを実行／データを読み出せるか、RAM 実行が要るか、割り込み・UART・USB・
  SysTick・watchdog にどの程度影響するか確認する。低消費電力・クロック変更との併用条件も調べる。

### 公開 API と層構成を比較する

- RAW のみ、固定長 blob／構造体の保存、EEPROM 互換、キーと値の保存、FS 上の保存を比較する。
  Preferences 相当を主 API にする案も、EEPROM 互換を追加する案も、採否は未決定とする。
- RAW の関数またはクラスと、キー保存クラスの2層構成を検討する。RAW 単独利用、内部専用の RAW、
  保存先を差し替えられる共通エンジン等も比較し、層を分けること自体のコード量を測る。
- RAW に領域情報・read／program／erase・範囲検査を公開する案を調べる。
  物理的な書き込み制約を公開するか、端数・未整列データを下層で吸収するかを比較する。
  自動消去・read-modify-write・退避を行う場合は、その RAM 量、停止時間、電源断時の意味を明確にする。
- アドレス指定は物理アドレス、予約領域からの offset、領域 handle のどれにするか。
  RAW とキー保存が同じ消去領域を壊し合わないための分割・所有規則と、複数利用者の扱いを検討する。
- C 関数＋構造体、薄い C++ クラス、仮想インターフェース、template／ビルド時選択を比較する。
  コアとライブラリの境界、未使用時にリンクされない単位、動的確保なしで利用できる範囲を調べる。
- キーは文字列／整数 ID／ビルド時定義、名前空間は有無を含めて比較する。キー全文、hash と衝突検査、
  型情報・長さ情報の保持による容量と互換性を評価する。ESP32 の名前・上限・全型の再現は前提にしない。
- 整数、bool、文字列、バイト列、構造体のどれを扱うか。構造体を保存する場合の padding・型幅・
  データ形式変更を利用者とライブラリのどちらが管理するか検討する。
- 保存タイミングを即時、明示 commit、まとめ書きから比較する。成功の意味、終了時の動作、
  未保存の変更、1キー更新と複数キー一括更新の保証を分ける。
- 未保存、型不一致、容量不足、未対応、破損、busy、書き込み失敗を区別する API を検討する。
  デフォルト値を返す便利さと、エラーを見落とさず扱えることの両方を考える。

### 保存方式とデータ保護の範囲を比較する

| 候補 | 調べる利点・費用・制約 |
|---|---|
| 1セクタ内の固定位置へ保存 | 小さい記録形式を作れるか。書き換え時の消去範囲、退避 RAM、再保存中の電源断で失うデータを評価 |
| 1セクタ内へ追記 | 消去回数を抑えられる条件、起動時走査、満杯時の動作を評価。消去・再保存の際に既存データをFlash内へ退避できない制約を明示 |
| 2領域の交互更新 | 旧データを残して更新する方式の予約量、有効容量、管理情報、復旧手順を評価。各領域は1セクタとは限らない |
| 複数セクタのログ・循環利用 | 有効データの移送、空き領域、消去回数の分散、処理時間・コード量を評価。2領域方式と別に比較 |
| RAM に全体を保持して commit | 読み書きの扱いやすさと RAM 消費を評価。容量の小さい系列で全量保持を避ける方法も比較 |
| 既存の保存エンジン・FS を利用 | 移植範囲、依存、最小領域、管理情報、RAM・コード量、ライセンスと保守を評価 |

- 検出と復旧を分ける。CRC、長さ・型検査、世代番号、有効記録の識別、冗長コピーをそれぞれ比較し、
  「CRC があるから復旧可能」「2セクタあるから電源断に耐える」とは扱わない。
- 新規追加、上書き、削除、領域整理、初期化のそれぞれで、電源断後に旧値・新値・未保存・破損のどの状態を許容するか。
  更新対象以外のキー、既存データ全体に対する保証も別に検討する。
- 1セクタの小容量構成を許容した場合、通常追記と消去・再保存で保護範囲が変わることをどう伝えるか。
  満杯なら失敗させる、利用者が明示的に整理する、RAM 退避して再保存する等を比較する。
- 保護方式を固定するか、容量から決めるか、容量とは別の選択肢にするか。非対応の組み合わせを拒否する方法と、
  利用者が現在の保護能力を確認する方法を検討する。必要最小セクタ数・予約量は方式ごとに算出する。
- 同値の再保存省略、更新頻度の違うデータの配置、消去回数の分散と追跡を比較する。
  管理情報そのものの書き込み費用・寿命も含めて評価し、単に「wear leveling 対応」で一括しない。
- 未初期化・既知形式の破損・未知の形式を区別するか、初期化・全消去をどの操作にするか。
  自動初期化による既存データ消失と、初回利用の容易さのどちらを優先するか検討する。

### 容量確保とリンカとメニューを検討する

- Flash 末尾の固定領域、専用 data 領域、ボード指定の固定領域、外部媒体を比較する。
  プログラム直後への配置はビルドごとに位置が変わる点を含め、保存位置の安定性を評価する。
- リンカの `MEMORY` でアプリ領域を縮め、保存領域の開始・終了をシンボルとして渡す案を調べる。
  コードだけでなく `.data` 等のロードイメージも含めて重複を防ぐ。専用セクションを設けるかも検討する。
  `NOLOAD` やシンボルによる予約だけでは、書き込みツールの消去から保護できない点を確認する。
- `tools/generate/generate.py`、variant の `.ld`、共通 `sections.ld`、`boards.txt`、サイズ表示・
  `getFreeSketchSpace()` 等に必要な変更を調べる。生成物の手編集ではなく、容量・境界の正本をどこに持つかを決める。
- 「末尾」は選択した型番の容量、`ANY` の最小容量、実行時検出した実容量のどれか。
  型番・予約量・bootloader 配置を変更した場合に位置が動くか、異なるビルド間で保持できるかを検討する。
- Arduino IDE のツールメニュー、arduino-cli の FQBN、上級者向けビルド設定による容量選択を比較する。
  保存なし、1セクタ、複数セクタを候補に含め、既定値は別途判断する。
- **容量を選ぶ場合は実際の予約バイト数を明示できる表示を検討する。** 「小／標準／大」やセクタ数だけでは
  アプリに残る量を計算できない。系列別の実バイト数とセクタ数、アプリ上限を対応付けて提示する案を比較する。
- 任意のバイト数指定を認めるか。消去境界への切り上げ・拒否、最小量、最大量、設定間の矛盾を生成時／リンク時に
  検査する方法を検討する。丸めた場合の実予約量を隠さない表示とするかも整理する。
- ライブラリを使う場合だけ領域を確保する案と、選択したメモリ配置を維持する案を比較する。
  Preferences を使わないスケッチへ書き換えたとき、その領域をアプリへ戻してよいかは保持方針と合わせて決める。

### バイト単位の費用を見積もる

- 予約 Flash、管理情報、キー・型・長さ、データ、書き込み単位への padding、削除記録、更新履歴、
  退避・回収に必要な空きを分けて、方式別に容量計算式と具体例を作る。
- 「予約量」「有効データの最大量」「現在の使用量」「そのまま追記できる空き」「整理後に再利用可能な空き」を
  区別する。文字列キー・可変長値では一定のペイロード容量を保証できるか、空き量 API をどう定義するか検討する。
- 1キーの小さな整数、複数の整数、文字列、固定長 blob の保存例で、予約量に対する有効データの割合を比較する。
  更新時には旧記録も残る方式で、読み出せるデータが収まっていても更新できないケースを評価する。
- 追加コード・定数、静的 RAM、heap、最大 stack、書き込み・整理時だけの作業 buffer を測る。
  小容量向けには全域 RAM コピーや常駐索引を省き、Flash を走査する案も時間との交換条件で比較する。
- 保存なし、RAW のみ、固定長保存、キー保存の最小構成、文字列等を含む構成を同じ toolchain・最適化条件で比較する。
  空の sketch との差分と実用的な sketch に足した差分、V003 と他の代表系列の両方を測る。

### アップロードと将来の形式変更を調べる

- WCH-Link／OEP 等の debug 経路、factory ISP、HID bootloader の各経路について、実際の消去単位・消去範囲、
  mass erase の有無、ELF／HEX／BIN の扱いを調べる。保存領域外の書き込みだけで保持できるとは仮定しない。
- 通常アップロードで保持、明示的な初期化で消去、といった選択肢を調べる。保存領域の情報をリンカ・生成器・
  アップローダで共有する方法を検討し、経路ごとに実現できない保証を区別する。
- 予約容量や型番選択の変更、保存形式の版変更、スケッチの更新・ダウングレードで、保持・移行・初期化のどれを
  行うか検討する。移行用の追加 Flash／RAM が用意できない構成も対象にする。
- schema version、未知キー、型変更、既定値、ライブラリ間の名前衝突をどこで管理するか。
  工場設定と利用者設定、設定の書き出し・復元を扱うかも用途と費用から判断する。

### 比較調査と試作評価の進め方

- 他コアの EEPROM エミュレーション、型付き FlashStorage、キー保存を比較する。ESP32 の Preferences と下層 NVS を
  分けて調べ、公開 API だけ参考にする案、既存エンジン移植、CH32 向けの小規模実装を同じ基準で評価する。
  比較資料の例: [ESP32 Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)、
  [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html)、
  [STM32duino EEPROM](https://github.com/stm32duino/Arduino_Core_STM32/wiki/API#eeprom-emulation)、
  [Arduino-Pico EEPROM](https://arduino-pico.readthedocs.io/en/latest/eeprom.html)、
  [SAMD FlashStorage](https://github.com/cmaglie/FlashStorage)、[Mbed TDBStore](https://github.com/ARMmbed/mbed-os/blob/master/storage/kvstore/tdbstore/include/tdbstore/TDBStore.h)。
- まず系列別の媒体仕様表と用途別の容量例を作り、候補方式の予約 Flash・コード・RAM・保証範囲を並べる。
  小容量系列で成立する候補を試作してから、全系列共通 API と系列別実装の境界を判断する。
- ホスト上の媒体モデルで、消去・書き込み単位、再書き込み制限、途中書き込み、容量不足、破損、
  更新・削除・領域整理の各段階での中断を再現する。実ハードウェアの制約と異なるモデルで保護を保証しない。
- 実機では境界・所要時間・割り込みへの影響・再起動後の読み出し・アップロード後の保持を確認する。
  電源断耐性を掲げる構成では、reset 試験とは別に実際の電源断を含めて評価する。
- 採否を決める時点で、対象系列、最小構成、利用者に見える容量、API、保存方式、保護の限界、
  メモリ配置・アップロード契約、測定した費用をまとめる。それまではこの項を検討 TODO として維持する。

## examples の充実：既存例・ESP32・CH32 EVT からの追加候補

- [ ] **機能ごとの最小例と、実際の部品・相手機器を使う例を追加する。** 下の候補を順に具体化する。
  既存例の説明拡充とは別に、スキャナやループバックだけでは学べない通信手順、状態の変化、失敗時の扱いを補う。
  表のスケッチ名は案であり、すべてを独立したスケッチにする必要はない。既存例への小さな追加で説明できるものはまとめる。

### 比較元と選び方

- このコアの `libraries/*/examples` と公開ヘッダを確認し、同じ機能の例がある場合は、その例で扱わない使い方を候補にした。
  `tests/` の検証コードは参考にできるが、配線・操作・結果を説明する利用者向け examples の代わりとはしない。
- 表中の `ESP:` は、指定された
  `~/.arduino15/internal/esp32_esp32_3.3.12_feadd113249efafd/libraries/` からの相対パス。
  `EVT(CH32系列):` は `~/dev_wch/CH32系列/EVT/EXAM/` からの相対パスで、例のディレクトリを示す。
  調査した SDK は CH32V003 / V006 / V103 / V20x / V205 / V307 / V407 / X035 / X315 / L103 / M030 / H417。
  同じ機能が複数の SDK にあるときは代表例だけを記す。これは全系列で利用可能という意味ではない。
- **A は既存 API で例を組み立てる候補、B は公開 API・ドライバ・ライブラリの採否や拡張も先に検討する候補。**
  SDK に例があるだけで、Arduino の API があるとは扱わない。実装側の検討は
  [FEATURE_CANDIDATES.ja.md](FEATURE_CANDIDATES.ja.md) と対応付け、着手時にソースと対象ハードウェアで確かめる。
  サポート状況・測定値・版ごとの制約はこの一覧へ複製せず、各ライブラリの説明へ集約する。

### 第1弾の実装

- [x] 既存APIだけを使う次の10例を追加し、日英README・概要と要点の英日コメント・生成profile・日英一覧への導線を用意する。
  - CH32RV: `SystemInfo` / `ResetReason` / `WatchdogRecovery` / `PwmFrequency` / `SerialLineReceiver` / `SerialPacketReceiver`。
  - Wire: `I2CRegisterRead` / `I2CSlaveRegister` / `I2CTimeoutRecovery`。レジスタ読み取りとslaveは2台で対にして試せる構成。
  - SPI: `SpiFlashId`。外部FlashのJEDEC IDを読み、消去・書き込みは行わない。
- [ ] 第1弾の実機確認：UARTの操作、PWM波形、I2Cの対向通信・相手不在・復旧、外部SPI FlashのIDを確認する。
  以下の候補表は機能全体の候補として残す。追加済みの例があっても、EEPROM・共有SPI・他のUART応用まで完了したとは扱わない。

### 第2弾の実装：物理入力とUSB HID

- [x] `ButtonDebounce` / `AnalogInputToPwm`と、`ButtonKeyboard` / `ButtonMouse` / `AnalogGamepad`を追加する。
  日英README、要点の英日コメント、生成profile、日英一覧をそろえ、基本入力からUSBへの学習順を案内する。
  USBボタンは安定した解放後に有効化し、再認識時も押したままの入力を抑える。
  送信失敗は最新の全状態で再試行し、gamepadは中央校正・中立帯・校正異常を扱う。
- [ ] 第2弾の実機確認：ボタンの押下・長押し・解放、可変抵抗とPWM、USB再接続時の中立、
  ゲームパッドの軸方向・両端・中立帯を、対象ボードとPCの確認画面で確かめる。

### A. 既存 API を使って追加する例

| 機能 / スケッチ案 | 既存例に足りない使い方・例で示すこと | 比較元・参考例 |
| --- | --- | --- |
| 基本入力：`ButtonDebounce` / `AnalogInputToPwm` | `Blink`・`Fade`から、押した瞬間だけ反応する入力、チャタリング除去、可変抵抗で明るさを変える入出力の組合せへ進む。`Timing`の経過時間比較を応用する。 | 既存 `Blink` / `Timing` / `PinInterrupt` / `AnalogRead` / `Fade` の間を埋める。 |
| システム情報：`SystemInfo` | `CH32RV`のチップ情報、CPU周波数、ヒープ情報、利用可能な場合の固有IDを表示する。固有IDとMACアドレスを区別し、任意機能の有無を判定する。 | `ESP:ESP32/examples/ChipID/GetChipID`、`EVT(CH32V003):RCC/Get_CLK`。 |
| 再起動と監視：`ResetReason` / `WatchdogRecovery` | `restart()`、`resetReasonName()`、`wdtEnable()`・`wdtFeed()`を使い、通常動作と意図的な給餌停止を比較する。再起動ループを避ける操作、開始後に停止できない監視の性質を説明する。 | `ESP:ESP32/examples/ResetReason/ResetReason`、`ESP:ESP32/examples/Timer/WatchdogTimer`、`EVT(CH32V003):IWDG`。ESP32のタイマによる監視の実装をそのまま移すものではない。 |
| 内部ADC：`InternalAnalogRead` | `analogReadTemp()`・`analogReadVref()`・`getVddMillivolts()`を使う。外部センサの温度とチップ内部温度、概算値と校正済みの測定値を区別し、未対応時も扱う。 | `EVT(CH32V307):ADC/Internal_Temperature`、既存 `AnalogRead` / `AnalogResolution`。 |
| PWM：`PwmFrequency` / `RgbLed` | `analogWriteFrequency()`で周波数とdutyを別に設定し、同じタイマを共有するピンへの影響を観察する。RGB LEDの配線、反転、明るさ補正を示す。 | `ESP:ESP32/examples/AnalogOut/ledcFrequency`、`ESP:ESP32/examples/AnalogOut/ledcWrite_RGB`、`EVT(CH32V003):TIM/PWM_Output`。LEDC専用APIは移さない。 |
| UART：`SerialLineReceiver` / `SerialPacketReceiver` | `SerialEcho`から、受信途中の保持、行末、長すぎる入力、長さ付きバイナリ、タイムアウトへ進む。`available()`・`read()`・`availableForWrite()`を使い、待ち続けずにLED等も処理する。 | `ESP:ESP32/examples/Serial/onReceiveExample`、`EVT(CH32V003):USART/USART_Interrupt`。ESP32の`onReceive`専用APIではなく、このコアの受信APIで書く。 |
| 複数UART・ピン経路：`SerialBridge` / `PeripheralRoutes` | UART同士の双方向中継と送信詰まりを扱う。`begin(..., rx, tx)`・`setPins()`・`setRoute()`で、選べる経路と不正な組合せを示す。Wire / SPIにも同じ考え方を展開する。 | `ESP:SPI/examples/SPI_Multiple_Buses`、各公開ヘッダのピン経路API。必要な周辺回路数はビルド条件で絞る。 |
| I2C master：`I2CRegisterRead` / `I2CEeprom` | `I2CScanner`の次に、レジスタ番号を書いて`endTransmission(false)`から`requestFrom()`する読み取り、返却長とNACKの確認、EEPROMのページ境界と書き込み完了待ちを示す。 | `ESP:Wire/examples/WireMaster`、`EVT(CH32V003):I2C/I2C_EEPROM`。 |
| I2C slave：`I2CSlaveRegister`と対になるmaster | `begin(address)`・`onReceive()`・`onRequest()`で小さなレジスタを公開する。このコアではcallbackが割り込み内で動くため、固定長の受け渡しに留め、表示や重い処理を`loop()`へ渡す。 | `ESP:Wire/examples/WireSlave`、`EVT(CH32V003):I2C/I2C_7bit_Interrupt_Mode`。ESP32例のcallback内ログ出力は踏襲しない。 |
| I2C異常と復旧：`I2CTimeoutRecovery` | `setWireTimeout()`・timeout flag・`clearBus()`を使う。相手不在と線が解放されない状態を分け、復旧の成否を表示し、自動で延々と再試行しない。 | 既存 `I2CScanner`にない異常系。`libraries/Wire/src/Wire.h`の公開API。 |
| SPI実機通信：`SpiFlashId` / `SpiSharedBus` | `SPILoopback`の次に、外部SPI FlashのID等を読み、CS、mode、周波数、ビット順を説明する。複数CSを持つ同一バスと、複数SPIバスを区別し、機器ごとにtransactionを組む。 | `ESP:SPI/examples/SPI_Multiple_Buses`、`EVT(CH32X035):SPI/SPI_FLASH`。最初のFlash例は消去・書き込みを行わない。 |
| ソフトウェアバス：`SoftI2CRegisterRead` / `SoftSpiDevice` | スキャン・ループバックだけでなく、同じ部品をWire / SPI版とソフトウェア版で使う。ピン選択、共有GND、pull-up、CPUを使う転送の違いを比較する。 | 既存 `SoftI2CScanner` / `SoftSPILoopback`、上のI2C / SPI実機通信例。 |
| Servo：`ServoPulseControl` | `Sweep`の次に、`writeMicroseconds()`・`attach()`・`detach()`と操作入力を示す。パルス幅の範囲、別電源・共通GND、PWM / toneとのタイマ競合を説明する。 | 既存 `Sweep` / `TimerResource`。 |
| USB入力機器：`ButtonKeyboard` / `ButtonMouse` / `AnalogGamepad` | 自動送信のデモから、ボタン・可変抵抗による操作へ進む。押下と解放、長押し、軸の中央・dead zoneを示し、ホストへの送信成功も確認する。 | `ESP:USB/examples/Mouse/ButtonMouseControl`、`ESP:USB/examples/Keyboard/KeyboardMessage`、`ESP:USB/examples/Gamepad`、既存 `Keyboard` / `Mouse` / `Gamepad`。 |
| USB MIDI：`MidiChannelsAndReceive` | MIDIチャンネルとUSB cable番号を区別し、受信メッセージでLED等を操作する。constructorでcable数を設定し、送信先cableを明示してPC上の複数portと対応付ける。 | `ESP:USB/examples/MIDI/MidiMultiChannel`、`ESP:USB/examples/MIDI/ReceiveMidi`、既存 `MIDI` / `MIDIController` / `MIDIInterface`。 |
| USBの継続動作：`UsbReconnect` / `VendorBulkPeer` | 既存のevents例・Host切断通知を踏まえ、送信待ちの扱い、切断後の状態破棄、再接続後の再開まで示す。Vendor転送はPC側の短い送受信例と組にし、部分転送と送信詰まりを再現する。 | `ESP:USB/examples/USBVendor`、既存 `SerialEvents` / `USBVendor` / `VendorBulk` / `DeviceInfo`。単なる通知表示を増やすものにはしない。 |
| USB Host HID：`HostGamepad` / `HostHidReports` | `onGamepad()`で軸・ボタンを読み、consumer / system controlやraw reportを機器ごとに識別する。既存Keyboard / Mouse例の外にあるreport descriptor、output / feature reportの用途も示す。 | `ESP:USB/examples/USBHostGamepad`、`ESP:USB/examples/USBHostHIDCombo`、既存HostのHID公開API。 |
| USB Hostの複数機器：`HostHubDevices` / `HostComposite` | hub経由の接続、複合機器の複数interface、機器ごとのaddressとportを表示し、片方を抜いても他方を扱える例にする。VBUSの供給条件、対象とするhub構成をREADMEに書く。 | `ESP:USB/examples/USBHostCDC_MSC_HID`、既存 `DeviceInfo`と`getDevices()` / `getHubInfo()`。 |
| USB PD：`PpsVoltageControl` | `ListProfiles`・`CycleVoltages`の次に、PPSの範囲を確認して`requestProfile()`へ電圧・電流を渡す。拒否、PPSのない充電器、抜き差しを扱う。入力処理を状態機械にしても、契約要求API自体は待つ呼び出しである点を説明する。 | `EVT(CH32X035):USBPD/USBPD_SNK`、既存PD例と公開API。電圧が実際に変わるので、負荷と基板の許容範囲を先に明示する。 |
| ネットワーク導入：`EthernetDhcp` / `UsbNcmNetwork` | `Network`の入口として`ETH`またはUSB NCMでリンクとIP取得を確認し、固定IP、DHCP、切断・再接続、待ち時間の上限を示す。`UsbNetworkPing`の手作りARP / ICMPから、IPスタックを使う例へつなぐ。 | `ESP:Ethernet/examples/ETH_LAN8720`、`EVT(CH32V307):ETH/DHCP`、既存 `UsbNetworkPing`。ESP32向けPHY設定ではなくCH32側の接続方法で書く。 |
| TCP / UDP / 名前解決：`TcpEchoClient` / `TcpEchoServer` / `UdpEcho` | `NetworkClient` / `NetworkServer` / `NetworkUDP`でPCと通信する。TCPの読み取り単位とメッセージ境界の違い、部分送信、UDPの宛先、名前解決失敗、再接続を示す。PC側の確認手順も用意する。 | `EVT(CH32V307):ETH/TCPClient`、`EVT(CH32V307):ETH/TCPServer`、`EVT(CH32V307):ETH/UDPClient`、`EVT(CH32V307):ETH/UDPServer`、`EVT(CH32V307):ETH/DNS`。 |
| 時刻とサービス公開：`NetworkTime` / `MdnsService` | `configTime()`・`getLocalTime()`で時刻取得の完了を確認し、未同期とタイムゾーンを扱う。`MDNS.begin()`・`addService()`・`addServiceTxt()`でPCから見つけられるサービスを公開する。 | `ESP:ESP32/examples/Time/SimpleTime`、`ESP:ESPmDNS/examples/mDNS-SD_Extended`。Wi-Fi接続部分を持ち込まずEthernet / NCMで示す。 |
| TLS：`TlsClient` / `MutualTlsClient` | `NetworkClientSecure`でCA検証、ホスト名、接続前の時刻確認、エラー表示を示す。次段階でクライアント証明書を扱い、証明書・鍵の寿命と保管先を説明する。固定の外部サービスや期限付き証明書への依存を避け、ローカル相手の準備手順を付ける。 | `ESP:NetworkClientSecure/examples/WiFiClientSecure`、既存`NetworkClientSecure`の公開API。通信路とメモリの実機確認を前提にし、未検証の組合せを動作済み例として配らない。 |
| 取り込みデータの利用：`AdcBlockStatistics` / `LogicPulseDecode` | `HsAdcStream` / `LogicStream`の取得APIを使い、平均・最小最大やパルス幅の解析へ進む。取得側と処理側を分け、バッファの所有期間、処理落ちの検出、停止・再開を示す。 | 既存 `HsAdcRead` / `HsAdcStream` / `LogicBurst` / `LogicStream`、`EVT(CH32H417):HSADC/HSADC`。 |
| タスク・同期の応用：`SensorConsole` | 個々のプリミティブの例から、周期計測・コマンド受付・表示の組合せへ進む。ISRから通知して、短い処理を順に進め、キュー満杯時にも止まらない例にする。 | `ESP:ESP32/examples/FreeRTOS/Queue`、既存TaskKit / SyncKit例。FreeRTOSのスレッドモデルを移すのではなく、このコアの実行モデルで構成する。 |

### B. API・ドライバ・ライブラリ整備と組にする例

ここは「SDKにあるのに例がない」機能を見落とさないための候補。採用を決めたものから公開APIを設計し、
Arduinoのピン指定・タイマ所有・割り込み・転送バッファの規約に合わせる。
SDKの初期化をそのまま貼り付けて、コアの管理する周辺回路を上書きするスケッチにはしない。

| 機能 | 追加したい例と先に決めること | 比較元・参考例 |
| --- | --- | --- |
| 通常ADCの連続・多チャンネル取得 | DMAのblock取得、チャンネル順、バッファ受け渡し、処理落ち。HSADCの例とは分け、通常ADC用の公開APIを検討する。 | `ESP:ESP32/examples/AnalogReadContinuous`、`EVT(CH32V003):ADC/ADC_DMA`、`EVT(CH32X315):ADC/ADC_ScanOut`。 |
| ADCのハードウェア連携 | タイマ / 外部入力による開始、injected変換、analog watchdogのしきい値通知、dual ADC。トリガ・DMA・タイマの取得と解放を定義する。 | `EVT(CH32V307):ADC/TIM_Trigger`、`EVT(CH32V003):ADC/AnalogWatchdog`、`EVT(CH32V307):ADC/DualADC_RegSimul`。 |
| ADC前段・OPA・CMP | 入力増幅、比較器のしきい値、比較結果の割り込み・PWM停止。入力電圧範囲、出力経路、対応するアナログ回路のAPIを決める。 | `EVT(CH32L103):ADC/ADC_PGA`、`EVT(CH32V006):OPA/OPA_PGA`、`EVT(CH32V006):OPA/CMP_BKIN`。 |
| DAC | 直流出力からタイマ同期の波形出力・DMAへ進む。PWMとの違い、更新率、バッファの寿命を示す公開APIを用意する。 | `EVT(CH32V307):DAC/DAC_Normal_OUT`、`EVT(CH32V307):DAC/DAC_DMA`、`EVT(CH32V307):DAC/DualDAC_SineWave`。 |
| タイマ入力 | input captureによる周期・duty測定、encoderカウント、外部clock。`PulseIn`の待つ測定、`PinInterrupt`のソフトウェア計測と区別し、既存のタイマ資源APIへ組み込む。 | `EVT(CH32V003):TIM/Input_Capture`、`EVT(CH32V003):TIM/Encoder`、`EVT(CH32V003):TIM/Clock_Select`。 |
| タイマ出力・同期 | one-pulse、output compare、複数タイマ同期、相補PWM・dead time・break。周期callbackは既存`TimerResource`を入口にし、簡易Tickerが必要かは別途判断する。 | `ESP:Ticker/examples/TickerBasic`、`EVT(CH32V003):TIM/One_Pulse`、`EVT(CH32V003):TIM/ComplementaryOutput_DeadTime`、`EVT(CH32V003):TIM/Synchro_Timer`。 |
| 低消費電力タイマ | LPTIMの低電力中の周期起床、PWM / one-pulse。通常タイマ、`millis()`、復帰後の時間の関係を定義する。 | `EVT(CH32L103):LPTIM/LPTIM_LP_WakeUp`、`EVT(CH32L103):LPTIM/PWM_OnePulse_SingleMode`。 |
| UARTの高速受信 | DMA + idleによる可変長受信、二重バッファ、エラーの通知。受信callbackの実行文脈、未処理データの所有権を先に決める。 | `ESP:ESP32/examples/Serial/OnReceiveError_BREAK_Demo`、`EVT(CH32V307):USART/USART_DMA`、`EVT(CH32V307):USART/USART_Idle_Recv`。 |
| UARTの専用モード | RTS / CTS、RS-485の送信方向制御、half-duplex / single-wire、同期転送。外付けtransceiverと電気的な接続を分けて説明し、必要なAPIを選ぶ。 | `ESP:ESP32/examples/Serial/HardwareFlowControl_Demo`、`ESP:ESP32/examples/Serial/RS485_Echo_Demo`、`EVT(CH32V003):USART/USART_HalfDuplex`、`EVT(CH32V307):USART/USART_SynchronousMode`。 |
| ソフトウェアUART | 短い送受信と他処理との共存。採用するライブラリ、タイミング制約、受信割り込み・タイマ競合を検討する。 | `EVT(CH32V003):APPLICATION/SoftUART`。 |
| SPIの専用モード | slave受信とmaster側の対になる例、DMA、hardware NSS、half-duplex、CRC。既存SPI APIの拡張か別APIか、対応範囲を決める。 | `EVT(CH32V003):SPI/SPI_DMA`、`EVT(CH32V003):SPI/FullDuplex_HardNSS`、`EVT(CH32V003):SPI/1Lines_half-duplex`、`EVT(CH32V307):SPI/SPI_CRC`。 |
| I2Cの追加モード | DMA、10 bit address、PEC、必要なら複数master。通常Wireとの相互運用・未対応時の扱いを決め、ニーズのあるモードから例にする。 | `EVT(CH32V003):I2C/I2C_DMA`、`EVT(CH32V003):I2C/I2C_10bit_Mode`、`EVT(CH32V307):I2C/I2C_PEC`。 |
| I3C | controllerによる機器列挙・転送、対応する場合のIBI / target。I2Cの例と分け、専用driverと公開APIを検討する。 | `EVT(CH32V407):I3C/I3C_DMA_Controller`、`EVT(CH32H417):I3C/I3C_Controller_Check_ZeroIBI`、`EVT(CH32H417):I3C/I3C_DMA_Target`。 |
| CAN / CAN FD | 2台での送受信、filter、bus-offからの復帰。transceiver・終端抵抗と対応形式を説明する。共通`HardwareCAN`の型だけでなく実際のdriverが必要。 | `ESP:ESP32/examples/TWAI/TWAItransmit`、`ESP:ESP32/examples/TWAI/TWAIreceive`、`EVT(CH32V307):CAN/Networking`、`EVT(CH32L103):CAN/CANFD`。 |
| I2S / SAI / DFSDM | tone出力、mic収録、codec loopback、デジタルfilter。USB Audioの生成データと区別し、DMAとaudio bufferのAPIを設計する。 | `ESP:ESP_I2S/examples/Simple_tone`、`ESP:ESP_I2S/examples/Record_to_WAV`、`EVT(CH32V307):I2S/I2S_DMA`、`EVT(CH32H417):SAI/SAI_Play`、`EVT(CH32H417):DFSDM/DFSDM_I2S_Audio`。 |
| RTC・backup domain | カレンダ設定、alarm、backup register、リセット後の保持。SNTPの時刻例と区別し、電源条件と時刻APIへの橋渡しを決める。 | `EVT(CH32V307):RTC/RTC_Calendar`、`EVT(CH32V307):BKP/BKP`。 |
| sleep / stop / standby | ボタン、RTC、LPTIMによる復帰、RAMの保持、復帰後の周辺回路の再開。USB / PD / ネットワークを動かしたまま眠れると仮定せず、サービスを止める手順も設計する。 | `ESP:ESP32/examples/DeepSleep/ExternalWakeUp`、`ESP:ESP32/examples/DeepSleep/TimerWakeUp`、`EVT(CH32V307):PWR/Stop_Mode`、`EVT(CH32L103):PWR/Standby_RAM_Mode`。 |
| 設定保存・Flash | 起動回数、構造体の設定保存、初期化・破損・容量不足の扱い。EEPROM / Preferences相当の方式と予約領域を決めてから例にし、内部Flash書き込み関数だけでユーザー領域を消さない。 | `ESP:Preferences/examples/StartCounter`、`ESP:Preferences/examples/Prefs2Struct`、`EVT(CH32V407):APPLICATION/EEPROM_Emulation`、`EVT(CH32V003):FLASH/FLASH_Program`。上の永続化検討と連動する。 |
| ファイルシステム・SD | ファイル作成・追記・読み戻し、ディレクトリ、取り外し、満杯時の扱い。SPI / SDIO / SDMMC / eMMCのblock転送と、ファイルシステムの採用を分けて決める。 | `ESP:SD/examples/SD_Test`、`ESP:LittleFS/examples/LITTLEFS_test`、`EVT(CH32V307):SDIO/SDIO_SD_FATFS`、`EVT(CH32H417):APPLICATION/FATFS_eMMC`。既存`MSCSdCard`はファイル操作の例ではない。 |
| USBストレージとファイルの共有 | Host MSCからファイルを読む、SDをDevice MSCとして公開する際の所有権切替。PCとスケッチが同時に同じFSを書き換えない構成を決める。 | `ESP:USB/examples/USBHostMSC_Test`、`ESP:SD_MMC/examples/SD2USBMSC`、既存 `MSCBlockDump` / `MSCSdCard`。FSとHost側block deviceの接続が前提。 |
| 更新・bootloader連携 | ファイルからの更新、USB DFU / Firmware MSC、失敗時の復旧。書き込み範囲、イメージ検証、boot手順を確定してから例にする。 | `ESP:USB/examples/FirmwareMSC`、`ESP:Update/examples/SD_Update`、`EVT(CH32V003):USART_IAP`。既存`DfuRuntime`による更新側への移行と、実際の書き換え処理を区別する。 |
| 外部メモリ | QSPI FlashのID・読み出しからmemory mapへ、FMC / FSMCのSRAM / SDRAM、PSRAMへ進む。起動時設定・リンカ・DMAが参照できる領域を決める。 | `EVT(CH32V205):QSPI/QSPI_FLASH`、`EVT(CH32H417):QSPI/QSPI_MemoryMap`、`EVT(CH32H417):FMC/SDRAM_16bit_DMA`、`EVT(CH32V407):PSRAM/PSRAM`。 |
| カメラ・表示 | DVPからのframe取得、SPI LCD、LTDC表示、GPHAによる描画。sensor / panel driver、frame bufferとメモリ要件を定義し、その後USB Videoへ実映像を渡す応用を作る。 | `ESP:ESP_Video/examples/dvp_camera`、`EVT(CH32V307):DVP/DVP_TFTLCD`、`EVT(CH32V103):SPI/SPI_LCD`、`EVT(CH32H417):LTDC/LTDC_Display`、`EVT(CH32H417):GPHA/GPHA_M2M_WithLCD`。既存`VideoCamera`はテスト画像の例。 |
| touch入力 | 単一キー、しきい値・基準値の追従、複数キー。外付け電極の条件と、採用するtouch library / callbackの扱いを決める。 | `ESP:ESP32/examples/Touch/TouchRead`、`ESP:ESP32/examples/Touch/TouchInterrupt`、`EVT(CH32V006):TOUCHKEY/Touch_EX001`、`EVT(CH32H417):TKey/TKey_8keys`。 |
| 精密な波形入出力・PIOC・ARGB | WS2812、IRの送受信、1-wireを題材にし、PIOC実行codeや専用driver、DMA / タイマとの関係を整理する。ESP32 RMTのAPIの移植とは分ける。 | `ESP:ESP32/examples/RMT/RMTWrite_RGB_LED`、`EVT(CH32X035):APPLICATION/WS2812_LED`、`EVT(CH32X035):PIOC/PIOC_NEC`、`EVT(CH32X035):PIOC/1_Wire`、`EVT(CH32X315):ARGB/Gradient`。 |
| CRC・乱数・hash | CRCの既知データ検算、RNGの失敗処理、採用した暗号libraryによるhash計算。`random()`と鍵生成用乱数を混同しない公開窓口を検討する。 | `ESP:Hash/examples/SHA2`、`EVT(CH32V307):CRC/CRC_Calculation`、`EVT(CH32V307):RNG/RNG`、既存 `RandomNumbers`。CRC周辺回路だけで暗号hashを実装できるとは扱わない。 |
| 電源・保護・診断 | PVD通知、電源低下からの復帰、WWDG、Flash / RAM ECC診断。通常のIWDGやリセット理由の例と分け、保護設定の寿命と回復手順を設計する。 | `EVT(CH32V003):PWR/PVD_VoltageJudger`、`EVT(CH32V003):WWDG`、`EVT(CH32M030):FLASH/FLASH_ECC`、`EVT(CH32H417):ECDC/RAM_Block`。 |
| clock・RAM実行・高性能コアの資源 | clock出力・校正、RAM実行、cacheとDMAの整合、IPC / HSEMによるコア間データ交換。コアの起動・クロック設定との境界、共有メモリと同期APIが前提。単独コアのTaskKit例で代用しない。 | `EVT(CH32V003):RCC/MCO`、`EVT(CH32M030):RCC/HSI_Calibration`、`EVT(CH32X035):RunInRam/RunInRAM_Select`、`EVT(CH32H417):CPU/ICache`、`EVT(CH32H417):CPU/IPC`、`EVT(CH32H417):CPU/HSEM`。 |
| 特殊な高速・専用インタフェース | UHSIF転送、SerDes、SWPMIの最小通信と相手側の例。必要なボード・相手機器が用意できる用途から、専用driverとAPIを検討する。 | `EVT(CH32H417):UHSIF/UHSIF_SLAVE`、`EVT(CH32H417):SerDes/FullDuxTrans`、`EVT(CH32H417):SWPMI/SWPMI_SwpCard`。 |

### 応用例の採否と追加順

- [ ] 最初は **I2Cレジスタ読み取り・slave、SPI実機通信、UART受信、システム情報・watchdog、PWM周波数**を追加する。
  手持ちの部品や2台のボードで確認でき、既存APIの使用方法を直接補えるものから進める。
- [ ] 次に **Ethernet / NCMの接続、TCP / UDP、時刻・mDNS、TLS、操作入力付きUSB HID、PPS**を追加する。
  相手側のPCプログラム・設定手順を含め、未接続・失敗・再接続の結果まで確認できる例にする。
- [ ] Bは[機能追加候補](FEATURE_CANDIDATES.ja.md)の採用順と合わせ、まずADC・タイマ・UART / SPIのDMA、
  RTC / sleep、永続化・ファイル操作など、複数用途の基礎になる機能から例を用意する。
- [ ] その上で、ESP32のHTTPClient / WebServerやEVTのMQTTを参考にしたHTTP取得・小さなWeb server・telemetry、
  SDKのETH↔UART中継、audio録音、camera表示などを応用候補にする。
  TCP / TLSがあるだけでHTTP / MQTTの公開libraryもあるとは扱わず、libraryの採用とメモリ・接続復旧を先に確認する。
  同様にESP32のWi-Fi bridgeを、そのままCH32の2つのネットワーク間のbridgeとして移さない。
- ESP32のWi-Fi / BLE / ESP-NOW / Matter / Zigbee / OpenThread / RainMaker、ULP、専用のOTA / provisionerは、
  同名例をそろえるためには追加しない。CH32の`EVT(CH32V20x):BLE`も含め、対応ハードウェア・通信stack・公開APIの
  採用を先に判断し、必要な用途が決まったら別項目にする。
- EVTのFreeRTOS / RT-Thread等のOS移植、CoreMarkやボード固有の製品デモは、一般向けexamplesへ一括で取り込まない。
  OS向けのqueue / semaphoreの題材はTaskKit / SyncKitの実行モデルで学べる形へ直し、計測や内部検証は`tests/`側へ置く。

### 各例を追加するときの完了条件

- [ ] `.ino`は英日それぞれ1行の概要から始め、重要な操作・状態遷移・エラー処理を初心者向けに説明する。
  本当に短い注記以外は`// en:`と`// ja:`を別行にする。使い方・配線・長い注意書きは`README.md` / `README.ja.md`へ置く。
- [ ] READMEは目的、必要な部品・相手、配線、操作、期待する結果、失敗時に確認する点を日英でそろえる。
  任意の外部libraryやPC側ツールが必要なら導入方法も書き、更新される対応表や内部実装の数値は重複して固定しない。
- [ ] 対応する周辺回路・ピン・容量を使うビルド条件と`sketch.yaml`を整備する。
  最小構成でのビルドと実機・相手側の確認を行い、条件付きのものは条件を明記する。
- [ ] 新しい例を[日本語一覧](docs/examples.ja.md) / [英語一覧](docs/examples.md)へ追加し、library READMEからもたどれるようにする。
  成功だけでなく、入力なし・相手不在・時間切れ・再接続・buffer満杯など、その例に関係する失敗を扱う。

## 製品ボード別のテンプレート（examples）

WeAct CH32X035 と UIAPduino Pro Micro V003 の2つは作った（libraries/CH32RV/examples/Board*）。板が増えたら同じ形で
足す（`board:` 宣言でその板だけビルドする）。

## USB

- **CH32RVUsbDevice は FS / HS / SS（H417 の USBSS）まで動くライブラリとして、EspUsbDevice の全機能を実装する。**
  済み: 速度ごとのディスクリプタ（FS / HS / SS）、NCM、オーディオ、ビデオ、SD カードの MSC（ブロックだけ）。
  V103 / V20x / V30x のドライバは TinyUSB の時代のもので、自前のスタックでは作り直し（下の移行の項）。残り:
  - 新しい系列の実機確認（ベンチの DUT の USB 端子を配線してから）。HSI から作る 48 MHz の精度、V103 の EP4〜7 の
    自動トグル、HS の PHY と HSE。
  - L103 / M103、M030、V205: CPU Clock のメニューで USB の作れるクロックを選べる（2026-10-22）。既定は M030 だけ上げた。
  - 第2世代 USBHS（V407 / V467、X305 / X315、V205）の dcd を新しく書く。**実機ベンチがそろってから検討する**。
    調査の下書きは work/usbport/others（git 管理外）。
  - H417（2026-10-20）:
    - 済み:
      - ボード（H415 / H416 / H417、V3F 単独、共有 SRAM で実行）。
      - USBSS のデバイスのドライバ（ビルドのみ）。
    - 残り:
      - USBSS の実機での続き。列挙と CDC までは確認済み。
        - EP0 の 96 バイトを超える応答が壊れる件は、クロックを 100 MHz に上げて直った（2026-10-21）。
        - SS を挿したままでは SWD が使えないので、書き込みは FS の DFU で行う。実験用スケッチは、SS で
          configured にならないと FS に移る（UART / CDC のコマンドでも切り替える。scratch の UsbFallback.h）。
      - USBHS のドライバ。PB8 / PB9 が SWD と兼用なので、デバッガを外した状態での確認が要る。
      - USBFS は済み（CH32RVUsbDcdUsbfsOtg）。DFU での書き換えも済み（ch32rv_flash）。
        - DFU を Arduino の upload に組み込む: 済み（ch32rv の `boot dfu flash`、platform.txt の ch32rv_dfu）。
          ch32rv 0.22.0 を同梱した（2026-10-21）。
          ch32rv 0.22.1 に上げた（2026-10-22。H417 の 2 コアの扱い、`dbg resume --all`、`--release-dm`、
          `--confirm-run pc`、`boot dfu flash` の busy 待ち、`isp --transport uart`）。H417 の eth / logic の試験は 0.22.1 で通った。
          - **arduino-cli からの書き込みを H417（FS）で確かめた（2026-10-21）。** 4 回中 2 回通った（30 KB を 0.12 s、
            目印の行と稼働時間で確認）。残り 2 回は、DFU_DETACH のすぐ後に `interface is busy`（errno 16）。DFU モードの
            機器は出ていて、手で `boot dfu flash --probe usb:3-4` を実行すると毎回書ける。upload の間も動く discovery が
            開いているのと重なっていると見ている。ch32rv のセッションに、EBUSY でのやり直しを依頼した。
          - arduino-cli で `-p` だけを渡すと口の種類が分からず、デバッグプローブ用の `ch32rv flash` が選ばれる。
            `-l ch32rv-app` と `--discovery-timeout 10s` が要る（IDE は discovery が種類を渡すので要らない）。
      - USBSS のホスト。
      - 既定のクロックは HSI×16 の 400 MHz、V3F（F_CPU）は 100 MHz にした（2026-10-21、EVT の
        SetSYSCLK_400M_CoreCLK_V5F_400M_V3F_100M_HSI と同じ）。25 MHz では USBSS の EP0 の 96 バイト目以降で
        32 バイトの重複が出ていた。100 MHz で直った（512 バイトまで一致、SS 経由の DFU も確認）。
        - **device-data c83cf8e で解決（2026-10-21 取り込み）。** V208 の Ethernet は features に入った。
          そのクロックは RCC ではなく `EXTEN_CTR.EXTEN_ETH_10M_EN`（bit2）で、clock_enables に無いのが正しい
          ——Ethernet のドライバを書くときはそこを立てる。V20x に RNG は無い（V20x/V103 の幽霊の
          クロック有効化 11 行が消え、コアでは使っていなかった）。
        - **device-data c83cf8e で解決（2026-10-21 取り込み）。** H417 の 400M の行に RCC_PLLMUL16・
          RCC_SYSPLL_GATE・RCC_SYSPLL_SEL=0（field を消すだけ）と fpre 列が入った。generate.py は
          `NAME=0` を読み、FPRE と Flash の SCK 分周を行から引く。FAMILY に残るのはどの field かの名前だけ。
          生成値は以前と同じ（boards.txt に差分なし）。
      - V5F を使う形（タスク周りの入れ替えと合わせて）。
        - **測定（2026-10-22、docs/h417-multicore.ja.md）。** 設計はまだ。
          - WAKEIP で起こすと、103 µs で V5F のコードが走った。ほとんどは .bss のゼロ埋めの時間。
          - V5F は 400 MHz で、同じ計算は V3F の 4.9〜5.8 倍速い。
          - 両コアの amoadd は失われない。HSEM の排他も働いた。
          - V5F は GPIO を 20 / 25 MS/s で、揺れなく読めた。
          - 注意: V5F が V3F のバンクを休みなく読むと V3F が止まり、待ち合わせていると両方が止まる。
          - 注意: ch32rv のコマンドは、選ばなかったほうのコアを止めたまま終わる。ch32rv-26 に伝える。
      - **VIO18 の電圧（2026-10-21）。** PA13〜PA15、PB0〜PB1、PB10〜PB14、PC0〜PC3、PC6〜PC12、PD0〜PD7、
        PD9〜PD15、PE0〜PE1、PE7〜PE15、PF0〜PF5、PF11〜PF14 は VIO18 から給電される。評価ボードでは VIO18 が
        VDDIO につながっておらず（R11 は未実装、P11 の 7-8 はジャンパなし）、内部の LDO の起動時の値（XO の
        判定で 1.2 V、実測 1.36 V）で動くので、これらのピンの High は 3.3 V にならない。PWR_CTLR の
        VIO_SW_CR（bit 9）と VSEL_VIO18（[12:10]、3 = 3.3 V）で 3.3 V にでき、PC0 / PC1 / PB0 の High は
        3.31 V になった。ボードメニュー VIO18 Supply（既定はハードの初期値）と CH32RV.setVio18Millivolts() を
        入れた（ユーザーの判断、2026-10-21）。実機で確認済み: 既定では起動時 0（ストラップ、High 1.25 V）、
        API で 1.25 / 1.83 / 2.55 / 3.31 V、メニュー 3.3 V では起動時から 3.31 V。
      - HSADC（scratch の HsAdc）: PLL_CLK / 4 = 100 MHz で 20.0 Msps を SS で連続して流せる（ホストが libusb の
        非同期転送を並べれば取りこぼし 0）。VIO18 を 3.3 V にすれば、出力の High で 1022〜1023、12 ビットの ADC と
        約 3 % 以内で一致する。手を離したピンは、サンプリング容量（1.1 pF）との電荷の分配で 1 µs ほどで落ちる。
      - GPIO のロジックキャプチャ（scratch の LogicCap）: タイマーの要求で DMA が INDR を読む形は、1 回に
        HCLK の 8 クロックかかり、要求が来ても処理中なら捨てられる。100 MHz で 12.5 MS/s まで、周期は正確で
        連続。DMA1 / DMA2、書き込み先（共有 SRAM / DTCM）で変わらず、DTCM ではかえって遅い。M2M（タイマー
        なし）は 5 クロック = 20 MS/s で、1 回取りのみ。間隔は DMA の速さで決まり、バスを取り合うと揺れる
        （16 ビットで約 320 周期中 6 か所）。UHSIF はハンドシェイクのある並列バス（libUHSIF.a はバイナリ）で、
        DVP は外部の画素クロックが要るので、配線なしのロジックアナライザには使えない。
      - **CH32RVHsAdc と CH32RVLogicCapture は、2026-10-21 に SS を抜いた H417 で確かめた**（SWD で書き込み、
        例 4 本。ストリームの 2 本は FS で）。見つけて直したもの:
        - HsAdc の `read()` が常に 1023: DMA が書いたメモリをコンパイラが知らず、先に書いた印 0xFFFF を返していた。
          DMA の後にメモリバリアを入れた（LogicCapture にも）。直した後は出力 Low 0、High 1018。
        - HSADC の DMA は、どちらのバッファにも DMA_LEN + 2 個の連続したサンプルを書く（8 → 10、200 → 202。
          方形波の位相がバッファの境目で続くので、重複ではない）。DMA_LEN を `samples - 2` にした。
        - `analogWriteFrequency()` が 256 段に固定して分周比を切り捨てていた（H417 で 100 kHz → 130 kHz）。
          周期を分周比とカウンタの長さに分けて、指定どおりの周波数にした。H417 と V307 で 2 Hz〜100 kHz を確認。
        - LogicBurst の例が `%.2f` を使っていた（既定の C ランタイムに無い）。
        - LogicStream の上限（`maxContinuousRate()`）を F_CPU ÷ 8 から ÷ 11 に下げた（2026-10-21、SS と FS で測った）。
          100 kHz の PWM を同じポートで取り込むと、12.5 MS/s では毎周期 1〜2 サンプル抜け（125 → 123〜124）、
          10 MS/s でも 1309 周期中 4 回抜けた。9.09 MS/s と 8.33 MS/s では、周期が計算どおりの組み合わせだけだった。
          以前「12.5 MS/s まで正確」としたのは、PWM が 130 kHz にずれていた頃の測定。
        - SS での速さ: HsAdcStream 41.8 MB/s（20 Msps、ホストが読む間の取りこぼし 0）、LogicStream 19.7 MB/s（9.09 MS/s）。
        - **キャプチャの深掘り（2026-10-21、docs/h417-capture.ja.md）。** 目標は 20 MS/s。TIM1 の 2 つの比較チャネルで
          DMA1 / DMA2 を半周期ずらし、ダブルバッファでバンクを分けると、20 MS/s を USB 40 MB/s で流しながら 1506 万周期で
          ずれ 1 回。抜けの原因は SRAM の 128 KB のバンクの取り合い。次: ライブラリの形を決める（2 本の DMA、バッファの置き場所、
          並べ直しをどちらでするか）。V5F が GPIO を読む形でも、SS で流しながら 20 MS/s（40.4 MB/s）・25 MS/s（50.5 MB/s）が
          続いたが、USB の DMA に押されて 1 サンプルが 1 クロック遅れることが周期の約 740 ppm（DMA の形は 30 秒に約 0.3 回）。
          docs/h417-multicore.ja.md。
      - SS を挿したままの復旧: `ch32rv --connect-under-reset reset`（WCH-LinkE の RST → NRST）でリセットできる。
        書き込みは失敗する（プローブの誤り 0x55）。止まったファームの調べ物には、同じ方法での読み書きが使える。
      - QingKe V3F では `csrr mcycle` が例外になる（実験のスケッチで踏んだ）。
      - OEP の probe にする件。このコアの側は入れた（2026-10-21、84b57b0）: `CH32RVCallback`（`[this]` の
        ラムダ）、Vendor の `writeDirect()` / `onRxData()` / `setRxDataBuffer()` / `setInterfaceProtocol()`、
        HID ベンダーの大きいレポートと `setUsage()`。H417 の SS で OEP の流し方を確認（IN 約 200 MB/s、
        OUT 52 MB/s）。**oep-probe-arduino への依頼は 2026-10-21 に dev-oep のセッションへ送った**（コア 0.2.1 に合わせる、
        最初は X035、H417 は後。下はその元の下書き）:
        1. `OepPlatform.h` / `Oep.h` に `ARDUINO_ARCH_CH32RV` の分岐: unit id は `CH32RV.getUniqueId()`、
           restart は `CH32RV.restart()`、chip は `CH32RV.getChipModel()`。
        2. `ProbeConfig` の保存先: このコアには永続化の層がまだない（TODO の「永続化」）。当面は保存なしで
           動く形にするか、`ch32rv_flash` の 1 ページを使う。
        3. `DirectBulkStream` を FreeRTOS（portMUX、heap_caps）なしでも動くようにする: このコアの
           コールバックは `loop()` / `delay()` の中から呼ばれるので、排他は割り込みの禁止で足りる。
        4. ロジック / アナログのキャプチャの backend を ESP32-P4 の PARLIO / ESP32・RP2 の ADC から切り離す。
           H417 の側の材料: HSADC（20 Msps、DMA のピンポン）、GPIO のタイマー → DMA（12.5 MS/s 連続）と
           M2M（20 MS/s、1 回取り）。
        5. RVSWD / SWD の bit-bang の backend（CH32 の GPIO）。
      - 工場出荷 ISP は、電源投入時の DP=high の条件がいるので、ソフトウェアからは入れない。
      - H417 の UART ISP（PB6/PB7）: ソフトのリセット（デバッグ線、NRST、システムリセット、BOOT_MODE + リセット）では
        入らなかった（2026-10-21、docs/h417-bench.ja.md）。電源の入れ直しは未確認（機材に触れられるとき）。入れるかどうかは別に決める。
  - ライブラリをボードマネージャーに同梱するか分けるか（下の「ライブラリの配布」）。
  - **USB の外で要るスタック（ファイルシステム・IP）。タスク周りの入れ替えが終わってから取りかかる**（USB スタックの
    入れ替えと同時にはしない）。ADR-0013 は「今はまだ入れない」という扱いで、入れないと決めたものではない。
    - ファイルシステム: MSC（デバイスの SD カード、ホストの `mscMount()`）が FAT につながらないのは不足として扱う。
      ブロックデバイスの抽象（SD・MSC デバイス / ホスト・RAM ディスク・内蔵 Flash）の上に FS / File の API を置く形を
      考える。実装は候補を実測して選ぶ（FatFs、FatFs の小型版、SdFat、littlefs など。ESP32 は FAT / SPIFFS /
      LittleFS を並べて選べる形）。Flash・RAM・速度・ライセンス・長いファイル名・書き込みの安全性で比べる。
      **2026-10-21 に着手。** 要件・候補・測った結果は docs/filesystem.ja.md（大きさ、電源断、NOR の摩耗、V307 での速さ）。
      残り: SD での実測（ベンチに SD が要る）、FAT を NOR に載せる摩耗分散の層、littlefs の設定（block_cycles、媒体の大きさ）を変えたときの値。
    - IP: **2026-10-21 に着手を決めた**（タスク周りの入れ替えを待たない）。方針と作業の順番は docs/network-stack.ja.md。
      対象は Ethernet の MAC を持つ系列（V208 / V307 / V317 / V407 / V467 / H41x）と、その系列の USB の NCM。X035 は対象外。
      lwIP 2.2.x（NO_SYS、raw API）で一式を作り、測ってから詰める。API は ESP32 の Network ライブラリに合わせる
      （`NetworkClient` / `NetworkServer` / `NetworkUDP`）。EspUsbDevice の `ipConfig()` / `dhcpServer()` / `dhcpClient()` /
      `beginNetwork()` / `localIP()` もこの上に作る。Ethernet の MAC のドライバは機材が要るので後。それまでは PC の tap で試す。
      - 2026-10-21 済み: lwIP 2.2.1 の取り込み（vendor/lwip.lock.toml）、`ch32rv_net_task`、Network ライブラリ（固定 / DHCP、
        DHCP サーバー、TCP / UDP、DNS、SNTP の `configTime()`、mDNS の `MDNS`）、USB の NCM のつなぎ（`NetworkUsbNcm`、
        `CH32RVUsbDeviceNet::beginNetwork()` など。受信の流量制御 `receiveGate()`）、PC の試験（tests/unit/network）。
      - 2026-10-22 済み: Ethernet の MAC のドライバ。H417 は実機で HTTPS まで。V407 / V467・V307・V317・V208 / V203RB は
        EVT から移してビルドまで（docs/network-stack.ja.md の「ほかの系列」）。
      - 2026-10-27: V307 の実機で `--case eth` が通った（DHCP、ICMP、TCP / UDP、HTTP。HTTPS は RAM 64 KB で TLS のヒープが
        足りず、記録だけ）。V407 は P4 の OEP probe の口 v407 につないだが、そのコンソール（dmseq）が reason 0x04 で開かず、
        ch32rv のセッションが調査中。
      - 残り: V407 / V317 / V208 の実機。V208 / V203RB は CPU Clock の `60MHz (Ethernet, no USB)` を選ぶ。V307 のチップの
        リビジョン 0x20 の回避策。V307 / V317 の HTTPS のヒープ（RAM 64 KB）。対象の系列の USB デバイスのドライバが
        来たら NCM を実機で確かめる。HTTP サーバー、MQTT / HTTP のクライアント。
      - TLS: Mbed TLS 4.1 に決めた（2026-10-21、比較は docs/network-stack.ja.md）。`NetworkClientSecure`（setCACert /
        setCACertBundle（ESP-IDF の形式）/ setInsecure / クライアント証明書）は PC の試験で通る。実機では H417 の `--case eth`
        で、ECDSA / RSA のサーバー証明書の検証、setInsecure、別の CA を断ること、乱数（コアの `ch32rv_rng_read()`）まで通る。V307 は RAM がぎりぎりで、`RAM_CODE_MOD`（Flash と RAM の
        分け方）のメニューが要りそう（ユーザーと決める）。TLS 1.3 も入れた（2026-10-21。既定は 1.2 と 1.3 の両方を名乗る。Flash 約 +15 KB）。
  - コアの `time()` / `gettimeofday()` / `settimeofday()`（2026-10-21、syscalls.c）: これまで半ホスティングの `ecall` で
    落ちていた。直した。`clock()`、`fopen()` なども失敗を返すようにした。X035 の実機で確認済み（2026-10-21: settimeofday の後に
    time / gettimeofday / strftime が経過どおりに進む、clock、fopen / remove が失敗を返す、ecall なし）。millis() の 49.7 日の
    折り返しの処理だけは未確認。
  - ほかの系列の `enterBootloader()`（工場出荷 ISP への入口）。X305 / X315 は X035 と同じ手順を入れた（2026-10-22、ビルドのみ）。
    H41x は試して失敗（docs/usb.ja.md）。V20x / V30x / V103 / L103 / V205 / M030 / V4x7 は BOOT_MODE が無く、BOOT0 の端子だけ。
  - デバッガ復帰時の `SystemInit()` のやり直しで USB のクロック設定が消える件の対策。
  - **UF2（MscFirmwareDisk）での更新はやる**（2026-10-20）。ほかのファームウェア更新のクラス（EspUsbDevice の
    FirmwareUpdate、DFU の Download、CDC / Vendor / ネットワーク経由）も実装予定。使わなければ費用がかからない形にする。
    UF2 は仮想の FAT を見せるだけなので、ファイルシステムのスタックを待たずに作れる。優先度は低いまま。Flash の書き換えの
    土台は「永続化」の項で作るので、それと USB スタック・タスク周りの入れ替えのあとに取りかかる。工場出荷の ISP より得があるのは、読み出し保護をかけたまま更新
    したいとき、道具を入れずにドラッグ＆ドロップ（UF2）で更新させたいとき、署名の確かめや A/B の巻き戻しが要るとき、
    USB 以外（ネットワークなど）から更新したいとき。下地に Flash の自己書き換えが要る。
  - EspUsbDevice との差（2026-10-20 の洗い出し）。デバイスの側は 2026-10-21 に埋めた: `onAnyControlRequest()`、
    記述子を取り出す API（`deviceDescriptor()` / `configurationDescriptor()` / `stringDescriptor()` /
    `hidReportDescriptor()` ほか）、バスのイベント（`onBusEvent()`）、コールバックのユーザーデータ（`CH32RVCallback`
    の取り込み）。X035 の usb-peer-classes で確認。残り: Info の例 3 本の移植、ホストの Audio / Video / Network、
    複数リスナー、キュー、64 ビット LBA、CCID の追加 API、例 30 本ほど。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。見込み（2026-10-21）: ブートローダーへ移る前の 20 ms の
  待ちが、印に `millis() | 1` を使っていたため 0 ms になることがあり、ステータス段の応答の前に抜けていた
  （85f96bb で直した）。H417 では、その後の DETACH → DFU を 10 回ほど続けて通した。PC ホストの X035 で確かめれば閉じられる。
- **USB デバイスを自作のスタックへ移す（2026-10-20 に自作と決めた。ADR-0012 の追記）。** X035 で先行し、ほかの系列の
  拡張点（`CH32RVUsbDcd.h` のドライバの表、`CH32RVUsbDevicePort.h`）と FS / HS / SS を見込んで作る。
  - 済み: コア（EP0・標準要求・SS の要求と BOS・速度ごとのディスクリプタと SS のコンパニオン）、X035 / X033 の
    USBFS ドライバ、全クラス（CDC、HID、MIDI、ベンダー + WebUSB / MS OS 2.0、MSC、CCID、NCM、UAC1 / UAC2、UVC、
    DFU ランタイム）。CH32RVUsbDevice は TinyUSB を参照しない。ビルド時の個数・バッファの define は廃止
    （クラスごとの上限、`setBuffers()`、`setTransferType()`、`maxPacketSize()`）。
  - 実機: X035 F8U6 で usb-peer / -classes / -media / -net、X035 C8T6 で usb-peer と usb-peer-host / -host-classes
    （ESP32-S3 相手）が通った。例は X035 で全部ビルドが通る。費用は docs/usb.ja.md（TinyUSB 版より小さい）。
  - ホスト（CH32RVUsbHost）も自作に移し、CH32RVTinyUSB・vendor/tinyusb.lock.toml・vendor_tinyusb.py を外した
    （2026-10-20）。
  - 済み: ほかの系列のデバイスのドライバ（V103、V20x の USBFS と USBD、V30x の USBFS と USBHS、第2世代 USBHS、
    X315 の USBSS。ビルドのみ）。H417 の USBSS は実機確認済み。ホストは USBOTG_FS の全系列（2026-10-22、ビルドのみ）。
  - 残り: X035 / H417 以外の実機確認。
- （経緯）**USB スタックを TinyUSB のまま拡張するか、自作するかを決める（基本は 1 つに統合する）。**
  - CherryUSB は見送りの方向（2026-10-20 の調査）。upstream 1.6.1（Apache-2.0）の WCH 向け移植は X035 / X033 に無く
    （USBFS は V20x / V30x の配置だけ）、デバイス側は割り込みの中でクラスのコールバックを呼ぶ。ホスト側は独立した
    スタックのスレッド（ハブ・列挙、既定 2 KB）とブロックする待ちが要り、OS 無しの OSAL が無い。コアの再設計
    （planning/core-redesign: 別スタックを持たない協調実行、通知は通常の文脈）と合わない。
  - TinyUSB の気になる点: パッチ 6 つ、EVT の代わりのヘッダ 4 つ（約 540 行）、ビルド時の define（tusb_config.h と
    上書き）が多い。ホストの HCD・系列ごとの包み・IRQ の振り分けはすでに自前。オーディオ・ビデオ・NCM は X035 では
    重く、優先度は低い（Pico や ESP32 の規模で便利な機能）。
  - **自作の最小構成の試作（2026-10-20、work/minusb、git 管理外）**: X035 USBFS + CDC-ACM 1 つ、約 600 行。
    ビルド時の define 無し（識別・文字列は `begin()` の設定、リングバッファは呼び出し側が渡す）。割り込みは
    SETUP の取り込み・終わった EP の NAK・トグルだけで、要求の処理・記述子・コピーは通常の文脈
    （`ch32rv_usb_task`）。OUT はリングに空きができてから ACK に戻すので、NAK で流量制御になる。
    - 大きさ（同じ CDC エコー、リング 256 / 256、USB 無しのスケッチとの差）: 自作 Flash +3.3 KB / RAM +0.9 KB、
      TinyUSB + CH32RVUsbDevice（DFU ランタイム無し）Flash +15.1 KB / RAM +4.1 KB。
    - 実機（X035 F8U6、ホストは ESP32-S3 の EspUsbHost）: 列挙・文字列・CDC の往復・ラインコーディング・
      2000 / 4096 バイトの往復が初回から通った。4096 バイトの往復は自作 67 ms、TinyUSB 117 ms
      （どちらも S3 側の上限の範囲。S3 は 64 バイトごとに 1 ms 待つ）。
    - 未確認: PC（Windows / macOS / Linux）での列挙（F8U6 の USB は S3 につないでいる）、サスペンド・
      レジューム、リモートウェイクアップ、通知 EP の送信、複数のクラスの組み合わせ、DFU ランタイム・MS OS 2.0。
  - **複合デバイスの試作（2026-10-20、work/minusb/MinUsbComposite）**: 汎用のコア（EP0・標準要求・クラス / vendor
    要求をインターフェースの持ち主へ・EP の送受信、完了は通常の文脈で関数へ）と X035 のドライバ（EP1〜3・5〜7、
    IN / OUT とも。EP4 は EP0 と共有なので使わない）、クラス 5 つ（CDC、ベンダー HID（レポート ID 6、
    CH32RVUsbDeviceHidVendor と同じ）、MIDI、vendor のバルク + 要求 0x10、DFU ランタイム）。構成は実行時に組み、
    `end()` → `begin()` で入れ替えられる。
    - 実機（ホストは ESP32-S3 の EspUsbHost）: X035 F8U6（P4 の OEP）と X035 C8T6（WCH-LinkE）の両方で、
      列挙（6 インターフェース）・CDC の往復と 4000 バイト・ラインコーディング・ベンダー HID の往復・MIDI の往復・
      再接続・構成の入れ替え（vendor のバルクと要求）・DFU_DETACH → 工場 ISP（1a86 / 4348:55e0）→ 自動で復帰が
      すべて通った。F8U6 で再接続 20 回（毎回 CDC の往復）と 4000 バイトの往復 10 回も失敗なし。
    - 大きさ（CDC + ベンダー HID + MIDI + DFU ランタイム、USB 無しのスケッチとの差）: 自作 Flash +7.9 KB /
      RAM +2.5 KB、TinyUSB + CH32RVUsbDevice Flash +21.0 KB / RAM +5.1 KB（TinyUSB 版は WebUSB / MS OS 2.0 の
      記述子 約 1.6 KB を含む。自作版の RAM は使わない EP の分まで取った EP バッファ 約 0.6 KB を含む）。
  - 残る確認: PC ホスト（Windows / macOS / Linux）での列挙（今の配線では不可）、MSC・CCID・NCM・オーディオ・
    ビデオ・WebUSB / MS OS 2.0（BOS）・サスペンド / レジューム・リモートウェイクアップ、ほかの系列のドライバ
    （V103、V20x の USBFS と USBD、V30x の USBFS と USBHS）、ホスト側（CH32RVUsbHost）の扱い。
- **UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす**。今はブートローダー（1209:b003）だけで使っている。
  rv003usb をそのまま持ってくるか、同じような処理を自作するかから検討する（ライセンス、CH32RVUsbDevice と同じ API に
  載せられるか、V003 の Flash 16 KB / RAM 2 KB に収まるか）。
- ホスト（`CH32RVUsbHost`）。
- **USB デバイスの実機試験のホスト役を PC から別のマイコンに替える**。試験は `tests/instrumented/usb`（`--case usb`）に
  あり、X035 で列挙・CDC / HID / MIDI の往復・`end()`→`begin()`・DFU_DETACH / 1200bps タッチでの ISP 移行を確かめている。
  ホスト役は今は PC（`PcUsbHost`）で、`ch32rv isp flash --entry auto` での書き込みの往復はまだ入っていない。
  - クラスを足すとき: 列挙・1往復・ほかのクラスとの同時宣言で足りる。アイソクロナス転送（オーディオ・ビデオ）は
    転送速度と取りこぼしも測る。
  - 系列を足すとき: dcd と PHY・クロックが系列ごとに違うので、大きめの双方向転送、抜き差し、サスペンド・レジューム、
    ブートローダーへの切り替えまで確かめる。
  - 接続先（ホスト役）を別のマイコンにする理由: 試験の条件を PC の OS やドライバに左右されないようにする（今のキーボード試験は HID のカーネルドライバを外して PC に入力が届かないようにしている）。
    - 当面は Raspberry Pi Pico。OEP から SWD で書き込み、本体の USB 端子は DUT のホスト専用にする（PIO-USB は不要）。
      試験の指示と結果は OEP 経由でやり取りする。
    - X035 のホストドライバを書いたら、X035 もホスト役にできる（`CH32RVUsbHost` の試験を兼ねる）。
    - 電源のスイッチは追加しない。VBUS は流さず、DUT も Pico も 3V3 で動かす（Pico は 3V3 端子に入れて 3V3_EN を GND に
      つなぐか、VSYS に 3.3 V を入れる。採用前にデータシートで確かめる）。
      CH32RVUsbDevice も TinyUSB の X035 用ドライバも VBUS を見ないので、D+ のプルアップだけで接続になる。試験の
      スケッチは `config.selfPowered = true` にする。
    - 抜き差しは、DUT の `device.end()` → `device.begin()`（D+ のプルアップを外して付け直す。ホストからは抜き差しと
      区別がつかない）と、Pico 側のポートのリセットで代える。サスペンド・レジュームは Pico が SOF を止めて起こす。
      電源の入れ直しは、OEP からのリセット（NRST / デバッグ線）で代える。
    - VBUS の有無に依存する動作は試せない（今のライブラリには無い。VBUS の検出を入れるなら別の確認が要る）。

## USB PD ソース（CH32RVUsbPd）の実機確認

ソース（`USBPDSource`、`CH32RVUsbPdSource`）は実装済み（ビルドとフレームの単体試験まで）。残りは実機での確認。
- X035 どうしの対向（シンク `USBPD` ⇔ ソース `USBPDSource`）。ソースにする WeAct の板は CC の 5.1 kΩ（R4 / R5）を
  外す（回路図では SB2 / SB3 で PC14 / PC15 につながる Rd。実物で確認）。試験のスケッチは両方のコールバックを
  登録して明示的に true を返す。
- 確かめること: Rd の検出と向き（表裏）、固定と PPS の契約、Get_Source_Cap、Soft_Reset / Hard_Reset、PPS の途絶
  （15 秒）、Reject（名乗っていない電流）、取り外し、コールバックが false を返したとき（VBUS: 接続しない、電圧:
  Hard Reset で 5 V に戻る）。
- 実際の電圧が出ているかは範囲外（基板側の仕事）。

## 系列で限られているもの（2026-10-15 の調査と対応）

[peripheral-support.ja.md](docs/peripheral-support.ja.md) の ○ / ◐ の中身。

直したもの（実機で確かめていないものは、その旨を書く）:
- V205・X305 / X315 の Serial / Wire / SPI / PWM: 端子ごとの AF 番号（AFIO GPIOx_AFLR / AFHR）を書くようにした。
  ルート表は AF 番号ごとに端子の組み合わせを全部並べ、FIX メニューにも出る。**ビルドのみ**。V205 / X315 の実機で
  Serial・Wire・SPI・analogWrite の信号が端子に出ることを確かめる。
- X033 / X035 の IWDG: LSI が無く HSI / 1024 で動くので、その周波数（46.875 kHz）を使う。X035 で 1 秒のリセットを実測。
- Serial6〜9: V205 の USART6〜8、V30x の UART6〜8、V407 / V467 の USART6〜10（Serial9 まで）。**ビルドのみ**。
- PWM: TIM4 / TIM5 の既定ピンを追加（tone / Servo が使うタイマーは除く）。タイマー管理に TIM8〜10（V30x）/ TIM8（V4x7）。
- ADC の内部チャネル: `analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`。X035・L103・V103・V307 で実測
  （VDD 3.2〜3.4 V。温度は L103 25 ℃、V307 24 ℃、V103 46 ℃ — V103 はデータシートの V25 が 1.1〜1.6 V と広い）。
- **L103 / M103 の analogRead() が約 620 カウント低かった**: EVT と同じ VDDA / 2 のオフセット補正を入れた（L103 で実測）。
  V20x / V30x の EVT にも校正値の扱い（Get_CalibrationValue: 校正直後の RDATAR を 2048 から引く）があるが、V307 では
  その値が 1728 で、当てると VDD が 2.64 V になり不自然（当てない今は 3.19 V）。入れていない。外部の電圧計と照合して決める。
- X033 の送信ポーリングを X035 と揃えた。

残り（方針待ち・新しい API・実機が要る）:
- クロック: 2026-10-22 に CPU Clock のメニュー（項目名に周波数と使えるもの）と取得の API（`getCpuFrequencyMhz()` /
  `getApbFrequency()`）を入れた。実行中に変える API は作らない（F_CPU を定数として使う箇所が約 20、動いている周辺の
  入れ直しが要る）。既定は M030 だけ 72 MHz（EVT の既定、実機で確認）に上げた。L103 / M103・V205 は 8 MHz のまま。
  L103 の 72 / 96 MHz は、OEP probe（RP2350）が動いている L103 に接続できない（2026-10-23。8 / 48 MHz と WCH-LinkE は接続できる。
  コアのクロックの設定は原因ではない。プローブ側は ch32rv / dev-oep が調査中）。試すイメージには `CH32RV_BOOT_HOLD_MS` を付ける
  （docs/upload-routes.ja.md）。ベンチの L103 は戻してあり、NRST は RP2350 の GP2（`l103.nrst`）。V407 / V467 のメニューは
  2026-10-27 に入れた（200 / 120 / 60 / 20 MHz、既定 200 MHz。device-data 53b3b2f の CFGR2 の SYSPLL と Flash の SCKMOD を読む。
  V407WEU6 で millis() と接続を確認、200 MHz で `--case eth` が通る。350 MHz（HCLK 175）は micros() の逆数が 32 ビットで
  正確にならないので外した）。残り: X315 / H41x のメニュー（PLL の源と FPRE の分け方が別）、HSE、各項目の実機確認（UART の速度・ADC・USB）。
  2026-10-22: M030 の既定 72 MHz が起動で止まった（device-data の行の SYSCLK=72 は HCLK で、PLL は 144 MHz 固定。生成器が
  hpre を読んでいなかった）。e3ebb34 で修正し、表は device-data 6fcf406 で直った。M030G8R7 で 72 / 48 / 24 / 8 MHz の
  UART 出力を ch32rv セッションが確認。ほかの系列の項目は実機未確認のまま。
- PD の他系列（L103 / M103、V205、X305 / X315、H41x、M030 の USBPD0）: 2026-10-22 に入れた（ビルドのみ）。BMC の
  クロックは F_CPU から計算する（EVT の表は 10〜240 MHz。8 MHz の L103 / V205 / M030 で通るかは実機で確かめる。
  試験は `--case pd`）。M030 の USBPD1 は未対応。
- PWM の remap 先と CHxN。TIM8〜10 の PWM は 2026-10-22 に入れた（V30x・V4x7・H41x、TIM1〜5 の端子は奪わない、ビルドのみ。`--case logic` の `logic_pin` でその端子を指定して確かめられる）。
- 新しい API: スリープ、永続化（上記「永続化の調査と構成検討」。API・方式は未決定）、WWDG、RTC、SPI のスレーブ、GPIO 以外の EXTI。
  - OEP からの依頼（2026-10-21、dev-oep。急ぎではない）:
    - SPI のスレーブ: マスターから来る CS 単位の転送を受け、用意したデータを返す。CS が上がったことを知らせるコールバック
      （またはポーリングできる状態）と、転送 1 回分の受信バッファ。
    - 汎用 ADC（H41x 以外、X035 / V307 など）の連続変換: タイマーまたは自走で一定のレートを保ち、DMA の 2 面バッファで
      受ける。CH32RVHsAdc の startContinuous / readBuffer と同じ形。1〜数チャンネル。
  - Wire のスレーブ: `onReceive` は STOP のときだけ呼ぶ。書き込みのあと repeated START で読みが続くと、`onRequest` が先に
    呼ばれる（書かれたバイトはその中で `Wire.read()` できる）。AVR の twi は repeated START でも先に `onReceive` を呼ぶので、
    合わせるか決める（未修正、実機の確認も未）。
- `analogReference()` はハードウェアに基準電圧の選択が無い。

## USB ホスト（CH32RVUsbHost）と ESP32-S3 との対向試験（2026-10-15）

- ホスト側の実機確認: CH32X035C8T6（WCH-LinkE FC928F068181）と ESP32-S3 e4b063b49f2c で `usb-peer-host` /
  `usb-peer-host-classes` が PASS。ベンチの WeAct 板の F8U6 はシリコンにホストが無い（`Unsupported` を実機確認）。
  V20x / V30x（OTG_FS のレジスタ配置）はビルドのみ。
- 準備できない MSC でデバイス全体が止まる: TinyUSB の MSC ホストは列挙中の TEST UNIT READY を上限なく繰り返し
  （upstream の TODO）、そのあいだ同じデバイスの HID や CDC もマウントされない。回数か時間で打ち切るパッチを
  入れるか決める（対向試験は S3 側でメディアを「あり」にして回避）。
- ホストで未対応: アイソクロナス（オーディオ・ビデオ）、USB ネットワーク（NCM / ECM）、MSC のファイルシステム、
  ハブ越しの低速デバイス（PRE PID の実機確認）、V103（旧 USBFS）と USBHS のホスト。
- 対向試験（X035 デバイス ⇔ S3 ホスト）は usb-peer / -classes / -media / -net の 4 つで PASS。PC ホストの試験
  （usb / usb-classes / usb-media）は、X035 の USB を PC に戻したときに使う。

### ESP32-S3 側（EspUsbHost 2.10.0 / EspUsbDevice 2.6.0）の挙動

対向試験で回避しているもの。一覧と回避のしかたは [tests/peer/usb/README.ja.md](tests/peer/usb/README.ja.md) の
「ESP32-S3 側の挙動」。版を上げたら、直っているかを確かめて回避を外せるか見直す。

## ライブラリの配布

CH32RVUsbDevice / CH32RVUsbHost / CH32RVUsbPd をボードマネージャーに同梱するか、別のライブラリとして分けるかを決める。
判断の目安は「CH32RV 向けのパッチや依存があるなら同梱、TinyUSB を使う他のボード（Pico など）でもそのまま使えるなら分離」。
今の依存（同梱寄り）:
- コアのフック（`loop()` / `delay()` からの `ch32rv_usb_task()`）、variant の生成データ（クロック許可、IRQ 番号）、
  `ch32rv_registers.h` を直接使う。
- TinyUSB は CH32RV 向けに手を入れた版（パッチ1つ、SDK の代わりのヘッダ、ドライバのラッパー、振り分け）。
- arduino-pico などは自分の TinyUSB（pico-sdk / Adafruit TinyUSB）を持つので、もう1つ入れると衝突する。
分けられる部分: クラスの API と実装（HID・MIDI・オーディオなど）の大半は TinyUSB のクラスの上にあり、系列に依存しない。
分けるなら「ボード共通の USB デバイス API」と「コアごとのポート層」に割る設計になる（EspUsbDevice との共通化も含めて）。

