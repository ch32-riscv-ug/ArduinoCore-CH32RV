# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

この文書はテストの計画の概要です。
ベンチの基準環境はネイティブ Linux です。通常のpytestはunit/とplatform/のボード不要検査を実行します。
実機契約はrun_hardware.pyの明示入口で、試験の相手のアドレス（CH32RV_PROBE か --probe）とプローブの記録から、流せる試験を選んで実行します。
初期 bring-up の明示実行は [独立した診断入口](../tools/diagnostics/core-bringup/README.ja.md) に置き、
通常の pytest に実機操作を持ち込みません。
[カバレッジ](../docs/test-coverage.ja.md)は実装とテスト定義の照合、
[README](README.ja.md)は workspace の操作を扱います。

## 保証対象

単位は機能名ではなく「条件、刺激、観測、期待結果」で定義した契約にします。
Wire の正常通信、NACK、バス回復、slave 受信、slave 応答は別契約です。
コンパイル成功、自己検査、外部観測、別実装との相互運用は互いに代用できません。

| 論点 | 保証するもの | 方法 |
|---|---|---|
| 定義・生成物 | device-data、pin/route、割り込み、board/options、固定依存 | ボード不要の検査・再生成 |
| ビルド・runtime | API、link、startup、constructor、heap、Print/Stream、境界値 | native / host core、全対象のビルド、実機起動 |
| コア基本機能 | GPIO、時間、UART、Wire、SPI、ADC、PWM と異常時の動作 | DUT、独立した peer / 計測器 |
| 系列・part・製品 board | instance、package の端子、クロック、reset、製品 pin map | 生成する build matrix と代表実機 |
| Arduino エコシステム | 配布物、FQBN、標準 API、外部 library、upload/discovery/monitor、HID | 隔離した Arduino CLI と実機 |
| USB / USB PD | データ通信の role/class/復旧、または CC 交渉と実電圧 | USB peer、PD source、電圧計測 |

各ケースには安定した contract ID、対象 API、前提、刺激、期待値と許容差、観測方法、必要 capability、
対象 profile、終了時の安全状態を付けます。「全 API を呼んで PASS」だけでは保証にしません。

## 実機で確かめる範囲の決め方

ボード、型番、CPU Clock と、opt・Floating Point・parseFloat・Clock Source・route・コントローラーのメニューを掛け合わせると、
1 つの sketch でも組み合わせは数千を超えます。全部を実機で確かめることはできないので、層に分けます。

1. **組み合わせはビルドで確かめる**（CI）。例の全プロファイル、各系列の CPU Clock の全項目など。容量のはみ出しとコンパイルの失敗はここで捕まえる。
2. **ロジックはホストの単体試験**（tests/unit）。生成器、ring buffer、deliverEvents、時刻、PD のフレームなど。
3. **表のデータは突き合わせ**。端子・route・クロック・DMA の表は device-data から生成し、データシートや EVT のヘッダとの照合を device-data の CI で確かめる。実機は使わない。
4. **実機はコードの分かれ道を 1 回ずつ通す**。組み合わせではなく分岐を数える。
   - 周辺の IP の世代ごとに 1 回（USBFS の 3 種類、USBHS の第 1 / 第 2 世代、SS、Ethernet の 10M と 10/100M など）。
   - 生成されたクロックの手順の型ごとに 1 回（PLL の型、H41x の SYSPLL、V407 の CFGR2、X315 の手順を書き出す型など）。
   - 端の値を 1 回（最低と最高のクロック、RAM の最も小さい系列）。
   - 効き合うと分かっているものだけ組で試す（USB × クロック、タイマーの取り合い × PWM / tone / Servo など）。
5. **確かめた範囲を記録する**。「実機で確認」と「ビルドのみ」を周辺機能表に書き分け、確かめていない組み合わせを確認済みのように見せない。

常設ベンチ（[BENCH.ja.md](BENCH.ja.md)）の選び方（ファミリーごとに機能の一番多い型番、型番で分かれるコードには境界の型番）は、4 に沿っています。

## 試験の段階と役割（2026-11-14）

機能は基本すべて作ります（作らないのは特殊なもの、今は BLE だけ。ユーザー）。分母は silicon の機能で、実装が無いものは
「未実装」として表に残し、「対象外」にしません。実機や相手が無くて確かめられない機能も、先に実装し、試験（sketch と
pytest）と配線の案まで用意して「配線待ち」にしておきます。

試験は、いつ・誰が回すかで 2 つに分けます。

| 種類 | 回し方 | 中身 | 結果の置き場所 |
|---|---|---|---|
| 自動（回帰） | 変更のたびに CI か、区切りごとに常駐ベンチで run_hardware.py。人の手が要らない | 段階 0〜3 | junit と coverage.py の報告 |
| 単発（立ち上げ・計測） | 新しい機能の立ち上げ、精度・速さ・長時間の測定、機材を付け替える試験。人が機材を用意して 1 回ずつ | 段階 4〜5 | 測定の日付と条件を、その機能の文書（docs/ や README）に書く。coverage では「単発」の表 |

| 段階 | 何を | 役割 | 例 |
|---|---|---|---|
| 0 ボード不要 | ロジック、生成物、表の照合 | ホストの単体試験（tests/unit、tests/platform） | 生成器、ring buffer、PD のフレーム、coverage の数え方 |
| 1 ビルド | 組み合わせ（系列 × メニュー × 例） | CI | 全例 × プロファイル、CPU Clock の全項目 |
| 2 自己検査 | DUT が自分のレジスタと端子を読み返す。配線なし | DUT だけ（selfcheck、selfbus、selfsys） | クロックの設定値、PWM を自分の入力で測る |
| 3 外部観測と対向 | 線の上の波形と、独立した相手との通信 | OEP プローブ（WireSkein が判定）、対向（Pico、ESP32-S3 / P4、CH32RV どうし） | uart、wire、spi、usb-peer |
| 4 立ち上げ | 新しい機能を初めて実機で動かす。手で命令を送り、ログと取り込みで確かめる。通ったら段階 3 の自動の試験にする | 開発者、対向、プローブ | USBSS ホストの各段、新しいペリフェラル |
| 5 計測・耐久 | 精度、速さ、長時間、電源断。外部の計測器や専用の機材 | 計測器、専用チップ（壊す試験はベンチでしない） | ADC のカーブ（外部の電圧源）、USB の転送速度、フラッシュの寿命 |

- 新しい機能は、実装と同じ変更で段階 0〜1 の試験を、実機で通したら段階 3 の自動の試験を足します。段階 4・5 の測定は、
  日付と条件を文書に残します。
- 自動の試験が見ない問い（精度、長時間など）は、試験の文書に「未検査（段階 5 で測る）」と書き、通ったことにしません。
- coverage.toml は、silicon にあってコアに無い機能も項目にし、報告に「未実装」と出します。

## ディレクトリ案

上位は必要設備、下位は契約で分けます。

```text
tests/
  unit/                         ボード不要: 定義・純粋ロジック・host core
  platform/                     toolchain: targets / examples / package
  single/<feature>/<case>/      DUT のみ
  loopback/<feature>/<case>/    DUT と折返し配線
  peer/<feature>/<case>/        DUT と対向機
    peer_reference/             対向機 sketch と独立した profile
  instrumented/<feature>/<case>/ DUT と刺激・波形・電圧計測
  manual/<feature>/<case>/      人の操作または臨時設備
  fixtures/                     共有設備要件・安全なテンプレート
  harness/                      設定解決・結果保存の最小限の補助
  diagnostics/                  準備・接続確認（合否テストではない）
  sketch_support/               sketch 共通の制御 protocol
```

single/runtime、instrumented/uart・gpio・powerに最初の実機契約を配置します。通常の pytest はボード不要の範囲だけを選び、
実機書き込みは設備群を明示選択します。手動は高度なテストではなく、人や臨時設備が必要なテストです。

通常の Python 検査は sketch と別ディレクトリに置きます。同じディレクトリの .ino が build/upload を開始するためです。
unit 内の host core sketch は物理ボード不要の profile に限定します。
DUT/peer lifecycle、port/profile 解決、build mode、lock は plugin 標準を優先し、
独自 peer framework や全体に効く autouse DUT fixture は作りません。基本は 1 module に 1 scenario とします。

## 対象と設備

silicon capability、part/package の端子、コア実装、board 配線、fixture の刺激・観測能力を分けます。
系列代表の実機成功は、その系列の全 part/route/instance の保証ではありません。

| 設備 | 主な責務 | それだけでは保証できないもの |
|---|---|---|
| P4 と各系列の代表 DUT | 起動、共通 API、外部刺激・波形、系列固有資源 | 未装着 part、未配線 route、独立アナログ基準、USB 相互運用 |
| LinkE と代表 DUT | upload/reset/monitor と基本動作の経路差 | P4 capture を前提とした精密判定 |
| Link と CH32V103 | V103 の経路別回帰 | 他系列の経路 |
| UIAPduino HID | 製品 pin map、HID upload、再接続、sketch 起動 | sketch 内 USB API |
| FS の DUT（X035・H417 など）と RP2040（Pico） | FS の USB host/device 両側の契約と、異なる実装（TinyUSB）との通信。同じ OEP プローブで書き込みと RTT コンソール | アイソクロナス（RP2040 のホストに無い）、他 controller/speed、PD |
| FS の DUT と ESP32-S3 | FS の USB host/device 両側。ESP-IDF の独立したスタックとの比べ役、アイソクロナス（usb-peer-media） | 他 controller/speed、PD |
| HS の DUT（V407 など）と ESP32-P4 のボード | HS の USB host/device 両側（P4 の OTG HS の口）。OEP プローブの P4 とは別の基板 | FS・SS、PD |
| DUT と安価な peer（未着手） | 指定通信契約の繰り返し検証 | 独立実装との相互運用の代替 |
| PD source と電圧計 | CC 交渉、VBUS、電源復旧 | USB データ通信 |

現構成の経路代表はLink＋V103、LinkE＋V307、ESP32＋UIAPduino V003です。
P4の系列代表とは別に、配線済みのUARTとGPIO、製品HID、PPPSの復帰を明示実行します。
系列代表を共通 API の基準とし、小 RAM、別 ABI、複数 ADC/I2C、DAC、別 USB controller、製品 pin map など
実装差分の理由がある対象を追加します。build は生成対象全体、実機は代表と差分を確認し、
未検証 part/route は明示します。clock/baud/mode/buffer 境界の組合せは契約ごとに選びます。

## 環境分離

共有するのは論理信号名、必要機能、安全条件、一般的な許容差です。
個体 ID、port、実配線、電源操作先、計測器、実測補正値はローカル設定に置きます。
配線テンプレートは共有できますが、特定個体の識別子や実測値は含めません。

拡張時のfixture設定は、共有の安全な default → Git 管理外の *.local.toml → 環境変数 → CLI の順に解決する設計です。
現在の入口は試験の相手のアドレス（CH32RV_PROBE か --probe）だけです。ほかはプローブ、チップ、プローブのメモ欄の記録から読みます（README の「実機契約」）。
相対パスの基準、未知キー、型、範囲を検証し、配列の置換とテーブルのキー別上書きを定義します。
接続先 default は空にし、別ボードへ書き込める default を作りません。ローカル探索場所を限定します。

port は plugin の規約を維持します。

- primary: CLI → `TEST_SERIAL_PORT_<PROFILE>` → `TEST_SERIAL_PORT` → sketch profile の port。
- peer: `--peer-port name:port` → `TEST_SERIAL_PORT_PEER_<NAME>_<PROFILE>` → `TEST_SERIAL_PORT_PEER_<NAME>` → peer profile の port。
- peer profile は primary を継承せず、`--peer-profile name:profile` または peer の default profile で指定します。
- .env は明示的に uv run --env-file .env ... で読みます。ローカル設定からの port 引渡しは harness で一本化します。

pin は I2C_SDA / SPI_CS / UART_RX などの論理名で DUT と対向側の pad に対応させます。
GPIO の数値を board 共通の pin と仮定しません。build define を使う場合も必須値を検証します。
build_config の未設定環境値は空文字になり得るため、それを正常な配線として扱いません。

Linux の udev/アクセス権、安定した identity、再列挙後の再解決、排他、給電、GND を前提検査にします。
WSL/USB-IP はベンチ標準手順に含めません。Arduino CLI の data/download/user、生成物、cache は隔離し、
通常の sketchbook や ~/.arduino15 を変更しません。DUT は作業ツリー、外部 peer core/library/tool は固定して検証します。
実際の core 所在、commit/dirty、tool/firmware を記録し、PATH の新しい版を暗黙に選びません。

## 判定と証拠

既存ケースは再利用を前提にせず、次の条件を満たすかで採否を決めます。

- API の出力と期待結果に因果関係がある。呼んだ直後の無条件 PASS は除外する。
- 同じ時刻源・controller・codec だけで正しさを証明しない。
- 初期化と安全な終了処理をケースごとに行う。upload/reset が外部機器や全周辺状態を消すと仮定しない。
- 単独・全体・順序変更で結果が変わらない。
- 機能判定と時間精度を分け、計測分解能、負荷、許容差を明示する。

primary 未設定は環境エラーです。plugin の未解決 peer は skip ですが、必須設備群では
「必要契約が未実行」としてゲートを通しません。任意設備不足、silicon に機能なし、コア未実装、
テスト未定義を同じ skip にまとめません。候補 firmware の READY 失敗を根拠なく設備故障にしません。

結果は contract × target × environment × observation ごとに PASS / FAIL / 環境エラー / 未実行 / 対象外を保存します。
core/sketch/tool/firmware、解決済み設定、build/flash と両側ログ、波形・measurement、skip 理由を紐付けます。
認証値は除外し、個体情報を含む実行 artifact をソース管理へ混ぜません。
実装・テスト定義と実行結果は分離し、リリース状態、テスト総数、機能総数は管理しません。
全対象 build の結果収集は fail-fast を避け、必須対象の合否ゲートとは分けます。

常設 HIL では未信頼 fork の firmware を直接実行しません。承認した source/artifact を入力にします。
DUT の lock だけでなく、共有 probe・電源・計測器も排他対象にし、異常終了時も出力と給電を安全状態へ戻します。

## USB peer 計画

USB データ、USB PD、HID bootloader は別契約です。TinyUSB source の存在を Arduino USB API 実装済みとは扱いません。

host と device は両方を検証対象とし、役割をボード名で固定しません。
DUT host / peer device と DUT device / peer host を対等な別ケースとして定義し、
それぞれの role 用 profile、契約、実行結果を分けます。一方の成功を他方の保証にしません。
ケースごとに board/firmware/driver/VBUS が指定 role を満たすことを確認します。
host と device の組を成立させ、同じ role 同士の接続を通信検証として扱いません。
OEP プローブ（ESP32-P4）の書き込み・観測と、USB の対向（ESP32-P4 のボードなど）は別の役割で、別の基板です。

| 契約 | 観測と期待結果 |
|---|---|
| 列挙・control transfer | descriptor/configuration、request 応答、無効 request |
| CDC / bulk | 双方向の既知 payload、連番/長さ/CRC、packet・buffer 境界、short/zero-length |
| HID / interrupt | report descriptor/report の一致、入出力、採用する class request |
| 復旧 | bus reset、切断/再接続、stall/clear、peer reset 後の再通信 |
| コアとの共存 | timer/UART 等との同時動作 |

復旧のうち stall/clear と peer reset 後の再通信、コアとの共存は、まだ試験がありません（いまあるのは end() / begin() と電源の入れ直しでの再列挙）。

API/class の採用範囲が未確定なものは、契約確定後に実装対象へ加えます。
両側の制御ログは被測定 USB と別経路にします。両側を書き込み、READY 問合せを完了してから USB を有効にします。
primary が先に upload されるため、setup 直後の通信開始は peer upload 中の reset を誤検出します。
状態を再問合せ可能にし、開始応答の待受けで列挙ログを読み捨てないようにします。

VBUS の給電元、逆流防止、切断方法、GND は wiring と安全条件に含めます。
FS のペアで HS を保証しません。安価な CH32X035 peer に替えても contract ID と payload を維持します（安価な peer の case はまだありません）。
同じ stack 同士に共通の不具合があっても通るため、ESP32S3 等との独立相互運用検証は残します。

速さごとの対向（2026-11-10 の方針）。どの速さも、DUT は host と device の両方を対向で確かめます。PC 直結は
初期の開発に使い、最終の検証は対向を主にします。

| 速さ | 対向 | 状態 |
|---|---|---|
| FS | RP2040（Pico）がアイソクロナス以外の全部を両方の役で。ESP32-S3 は別のスタックでの確かめとアイソクロナス。S3 の DUT はホストを持つ X035（C8T6）に替える | X035 と Pico で 9 ケース（media 以外。host-input・host-storage は Pico のみ）。ほかの FS のボードにも Pico をつないでいく |
| HS | ESP32-P4（より安い HS のボードに替えることもある）。H417 は SS と同じ口で HS の線が SWD と兼用のため、常駐のベンチにしない | コアの第 2 世代 USBHS はデバイスとホスト（V407 で実機確認）。第 1 世代はデバイスのみ |
| SS | H417 どうし。X315 の SS はデバイスのみなので、ホストの検証には使えない | コアの USBSS はデバイス（PC が相手）。ホストは全部の転送・ハブ・同じ口の USB 2.0 まで作る（2026-11-14、TODO）。SS の対向の case はまだ無い |

### USB の網羅の軸（2026-11-14 に作り直し）

今の USB の試験は「系列ごとにデバイスとホストが 1 回通る」形で、軸が足りません。PC が相手のデバイスの試験は、OS の
クラスドライバが使う機能しか通らない（SS の Mult>0 の等時、ストリーム、burst の窓、U1/U2 の入り方、STALL の後の動きなど
は通っていない）。ハブはどの速さでも試していません。次の軸を全部、表（coverage）の欄にします。

| 軸 | 値 |
|---|---|
| コントローラー | V103 の USB、USBD（V20x）、USBFS の 3 種（X03x / OTG_FS / V20x・V30x）、第 1 世代 USBHS（V30x）、第 2 世代 USBHS（V205・V4x7 ×2・X3x5・H41x）、USBSS（H41x・X315） |
| 役 | デバイス、ホスト、同じ石で同時（V407 の 2 つ、H41x の FS と SS） |
| 速さ | LS（ホストが LS の機器を、デバイスが LS で）、FS、HS、SS |
| つなぎ方 | 直結、ハブ 1 段、ハブ 2 段以上（5 段まで）。ハブの種類: FS の上流で動くハブ（LS は PRE）、HS のハブ（シングル TT とマルチ TT、FS / LS は split）、USB3 のハブ（SS 側と USB 2.0 側） |
| 転送 | 制御（長い・0 長・データなし・無効な要求）、バルク（0 長の終わり、短いパケット、SS の burst の窓 1〜16、ストリーム）、割り込み（間隔ごと）、等時（IN / OUT、同期・非同期・適応、フィードバック、SS の Mult 0〜2） |
| 省電力 | サスペンドとレジューム、リモートウェイクアップ、LPM（L1）、U1 / U2 / U3 |
| 異常と回復 | STALL と CLEAR_FEATURE、転送中のバスリセット、転送中の抜き差し、相手のリセット、NAK・NRDY が続く、タイムアウト、babble、warm reset（SS） |
| 数 | 1 台、ハブの先に複数（SS では ERDY に機器のアドレスが出ないので特に）、コントローラーの同時使用 |
| クラス | CDC、HID、MIDI、MSC（BOT と UAS）、ベンダー、CCID、NCM / ECM、UAC、UVC、DFU、ハブ |

### 相手の役割

どの契約も、**自作どうしの組**（全部の機能を両側から動かせる）と、**独立した実装の相手**（同じ思い違いを見逃さない）の
両方で通します。

| 相手 | 役 | 何に使うか |
|---|---|---|
| PC（Linux、xHCI） | ホスト（LS〜SS） | OS のクラスドライバでの普通の使い方。**libusb のベンダーの試験プログラム**で、クラスドライバが使わない機能（ストリーム `libusb_alloc_streams`、等時の任意のパケット数・Mult、burst、0 長、STALL、サスペンドの手順、LPM の sysfs）まで動かす。今の「PC が相手」の試験はクラスドライバだけなので、ここを足す |
| CH32RV どうし | ホストとデバイス | 両側の全部の機能。デバイス側で STALL・NAK・NRDY・切断を好きな時に起こせる。速さごとに: FS（H417 / X035 の FS どうし）、HS（V407 の USBHS1 と USBHS2、同じ石で）、SS（H417 どうし） |
| RP2040（TinyUSB） | FS のホストとデバイス | 独立した FS（アイソクロナス以外） |
| ESP32-S3 / ESP32-P4（ESP-IDF） | FS / HS のホストとデバイス | 独立した FS / HS、アイソクロナス |
| 市販の機器 | デバイスとハブ | USB3 メモリ（UAS と BOT）、USB2 メモリ、LS のキーボード・マウス、USB3 ハブ、USB2 ハブ（シングル TT とマルチ TT）。どれも個別に電源を切れる（PPPS、uhubctl）ものを選び、抜き差しを電源の入り切りで自動にする |

### 3 台の H417 のつなぎ方（案。USB のケーブルだけで、OEP の端子の線は変えない）

H417 は 3 台（A: 新しい台、LinkE で OEP に相乗り。B: 6-2 の EVB。C: 6-1 の nano）。nanoCH32H417 の USB3.0 の口は Type-A で、
1 番ピンは基板の VBUS の線に直結（スイッチなし、回路図 v1.0）、その口の USB 2.0 の線は USBHS（PB8 / PB9＝SWD）。FS は別の
Type-C（OTG）。

| 口 | つなぐ先 | 試すもの |
|---|---|---|
| A の SS（ホスト） | USB3 の PPPS ハブ（外部給電）→ C の SS（デバイス）、USB3 メモリ（UAS） | SS ホストの全部。自作の相手（C）と市販の相手（メモリ）を同時に置き、ERDY の取り違えとハブを試す。ハブのポートの電源で抜き差し |
| A の FS（ホスト） | USB2 の PPPS ハブ → C の FS（デバイス）、Pico（デバイス）、LS の機器 | FS ホストのハブ、LS（PRE）、複数の機器。A の SS と同時に動かす |
| B の SS（デバイス） | PC の USB3 の PPPS ハブ | SS デバイスを独立のホスト（xHCI）で。libusb の試験でストリーム・Mult・burst・U1 / U2 / U3 |
| B の FS（デバイス） | PC のハブ | FS デバイスを独立のホストで |
| C の SS / FS | A のハブ（上） | 自作どうしのデバイス側。ときどき PC につなぎ替えて同じ試験を流す（ケーブルだけ） |

- デバイスの台（B・C）の SS の口も Type-A なので、つなぐには A–A のケーブルが要る。両端の VBUS が基板の 5 V どうしで
  ぶつかるので、**VBUS を切った A–A のケーブル**（またはアダプター）を使う（DCD は selfPowered で VBUS を使わない）。
- A の SS の口の USB 2.0 側（USBHS）は SWD と同じ線なので、常駐のベンチでは使わない。USB3 ハブの USB 2.0 側の先の機器は
  A からは見えない（それは HS の試験で別に確かめる）。
- HS は H417 では常駐させず、V407 で: USBHS1（ホスト）→ USB2 の PPPS ハブ（マルチ TT）→ V407 の USBHS2（デバイス）、
  P4（デバイス）、Pico（FS、split）、LS の機器。P4（ホスト）↔ V407（デバイス）も。V307 の第 1 世代 USBHS は P4 と。
- ハブはどの速さにも要る（FS のハブの先の LS、HS のハブの split、SS のハブ）。

### ホストのコントローラーの配線待ち（2026-11-17、ドライバはビルドのみ）

case は今の usb-peer-host・-classes・-input・-storage のまま。どの口か・どの相手か・間のハブは、DUT の口の記録の `usb`
で決まる（RECORDS 4.3。ハブは `hub`、試験は `where` で相手がそのハブの先にいるかを確かめる）。

| 何を | DUT の口 | 相手 | 記録（例） | BOOT_HOLD |
|---|---|---|---|---|
| V103 の USB（旧 IP）のホスト | V103 の台の PA11 / PA12（USB の口） | Pico（FS）。S3 でも可 | `--usb rp2040,port=<Pico の口>` | 不要 |
| 第 1 世代 USBHS のホスト（HS） | V307 の USBHS（PB6 / PB7、HSE の板） | ESP32-P4 の OTG HS | `--usb esp32-p4,serial=...,mac=... --usb-port high_speed` | 不要 |
| H41x の USBHS のホスト（HS） | H417 の SS の口の USB 2.0 の線（PB8 / PB9 = SWD） | ESP32-P4 の OTG HS | `--usb esp32-p4,... --usb-port high_speed` | 要る（`begin()` で SWD が外れる。単発、常駐しない） |
| HS ハブの先の FS（split） | V407 の USBHS1 | USB2 の PPPS ハブ（マルチ TT）→ Pico | `--usb rp2040,port=<Pico>,hub=hs-mtt --usb-port high_speed` | 不要 |
| 同じ、シングル TT | V407 の USBHS1 | USB2 の PPPS ハブ（シングル TT）→ Pico | `hub=hs-stt` | 不要 |
| HS ハブの先の HS（split なし、確かめの対照） | V407 の USBHS1 | 同じハブ → P4 | `--usb esp32-p4,...,hub=hs-mtt --usb-port high_speed` | 不要 |

要る部品: USB2 の PPPS ハブ（マルチ TT とシングル TT、uhubctl で口ごとに切れるもの）、V103 の台の USB の口の線、V307 の
HSE 付きの板の USBHS の口、P4 をもう 1 台か付け替え。HS の split の LS の機器（キーボード）は手作業の単発。

### 試験の段階（USB）

| 段階 | 内容 | 自動か |
|---|---|---|
| 0 | ディスクリプタの組み立て、ハブの状態の解釈、split の計算、ルート文字列、ストリームの表 | 自動（tests/unit） |
| 1 | 全系列 × コントローラーのビルド | 自動（CI） |
| 3 | 上の軸の契約を、自作どうしと独立の相手で。case は「どのコントローラー × どの相手 × どのつなぎ方」を記録から決め、junit に controller・peer・hub を残して coverage の欄を分ける | 自動（常駐ベンチ） |
| 4 | 新しいドライバの立ち上げ（SS ホストの段 1〜8 など）。両側のコンソールで手で | 単発 → 通れば 3 へ |
| 5 | 速さ（MB/s）、長時間、抜き差し 100 回、PC の OS ごとの違い | 単発（日付と条件を docs/usb.ja.md に） |

## プローブとの責務分担

| 保持先 | 保証対象 |
|---|---|
| このコア | Arduino CLI recipe、DUT upload/reset/monitor、HID、公開 API、経路別の最小 E2E |
| ch32rv / プローブ | transport/protocol、転送境界、設定永続化、flash/debug 内部処理、firmware update |
| plugin / 計測側 | fixture lifecycle、port/peer 解決、lock、capture、decoder |

プローブ内部の検証一式は複製しません。既存 tcp_link のようなケースは DUT E2E とプローブ単体に分解します。
具体的な移動は相手リポジトリの入口と対応させてから行います。通常テストからプローブを勝手に書き換えません。

このコアは利用したfirmware版・protocol/interface revisionとhost tool/client版を結果に保存します。
プローブfirmwareの最低版をこのコアで一律に定義せず、ケースに必要な機能の互換性判断は所有側のtool/clientへ委ねます。
既知不良の判定・対応条件・更新imageと更新処理はプローブ提供側、設備個体と配線はhost-local設定で管理します。
版情報が取れない場合はunknownとして理由を記録し、最新版や互換性を推定しません。

## 実機待ち・落ちている試験

2026-11-05 の表に並べた試験（wire.slave、adc-stream、dac、system、Serial の経路、SPI スレーブ、TaskKit）は、
2026-11-14 までに常駐ベンチの系列で流れています。残りは次のとおりです。系列ごとの最新の結果は coverage.py の報告で見ます。

| 試験 | 状態 | 要るもの |
|---|---|---|
| periph.hsadc | H417 で流れて落ちる。常駐の H417 は HSADC の端子に線が無い | PF8〜PF10 のどれかに`<口>@<端子>`の線（PC0〜PC3 は VIO18 側なので使わない）。TODO |
| periph.adc-timer-trigger / analog-continuous | 未実行（2026-11-17 に追加、ビルドのみ） | adc と同じ ADC の線 1 本 |
| periph.adc-external-trigger | 未実行（2026-11-17 に追加、ビルドのみ） | ADC の線に加え、ADC のトリガの端子への`<口>@<端子>`の線（多くの系列はビット 11 の端子、X305/X315 はビット 12、V00x は PD3 か PC2、M030 は PA14 か PB6。DUT が tcaps で答える）。今のラベルにあるかは未確認 |
| pd | どの台でも未実行 | 常駐ベンチの PD の配線。TODO |
| wire.repeated-start（ハードの Wire、H417） | 6-2 で落ちる（H417 の SDA の保持とプローブの入力）。SoftWire は通る | TODO |
| USBSS の対向 | 試験が無い | ホストの実装と対向の配線。TODO |
| hid | 常駐ベンチでは流していない | UIAPduino の台 |
| selfexti（ピンでない EXTI の線と PVD） | ビルドのみ（2026-11-17）。自動の自己検査で、配線なし。wake は休止するので CH32_TEST_BOOT_HOLD_MS を付けて流す | 常駐ベンチの各系列 |
| PVD の本当の低下（単発） | 試験が無い。自己検査（selfexti.pvd）は閾値を VDD の上と下に置くだけで、VDD が下がる道は見ない。3.3 V の板で閾値を VDD の上に置けるのは V003・V103・X035 だけ | 可変電源で DUT の VDD を閾値の上から下へ（例: 3.3 V → 2.6 V。系列の動作範囲の中で）。`LowVoltageDetect` の例で `Low voltage #1` が出る電圧と、戻すときの電圧（ヒステリシス）を記録し、日付と条件を libraries/CH32RV/README に書く。AWU の系列（V00x・X035・M030）の線での起床も、ここで PVD を `enableExtiLineWakeup()` にして見る |

V103にDACは無い（RMのF103との比較表）。analogはV103でスキップのままでよい。

## 試験を足す順序

1. contract ID、capability/対象 matrix、設定 schema、安全条件を定義する。
2. Linux で隔離 build → 一意な DUT へ upload → READY → GPIO 外部観測 → 証拠保存を通す。
3. 共通 API と系列差分を分け、SoftWire/SoftSPI、独立 peer による Wire slave、ADC/PWM 設定、DAC の不足を埋める（済み）。
4. USB host/device 両側の実装範囲を定義し、role 別に独立 peer の契約を実装する。
5. 経路別 Arduino E2E、package、外部 library を追加する。

完了条件は、対象契約を観測でき、必要 target/経路で実行でき、
設定と証拠から別の Linux 環境で再現できることです。

## 参照

設備別分類と peer/初期化の方針は
[基礎ガイド](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/main/TESTING_BASICS.ja.md)、
[詳細ガイド](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/main/TESTING_ADVANCED.ja.md)、
[peer の例](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/tree/main/examples/12_peer_host_core)
を参考にしています。plugin の peer skip と、この計画の必須契約ゲートは別の層です。
