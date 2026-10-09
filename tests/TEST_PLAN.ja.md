# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

この文書は再設計の概要です。新しい検査・CI・保守ツールは旧テストと設定に依存しません。
新ベンチの基準環境はネイティブ Linux です。通常のpytestはunit/とplatform/のボード不要検査を実行します。
実機契約はrun_hardware.pyの明示入口で、local TOMLの接続先と配線を検証して実行します。
初期 bring-up の明示実行は [独立した診断入口](../tools/diagnostics/core-bringup/README.ja.md) に置き、
通常の pytest に実機操作を持ち込みません。
[カバレッジ](../docs/test-coverage.ja.md)は実装とテスト定義の照合、
[README](README.ja.md)は新 workspace の操作を扱います。

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
| CH32X035 と ESP32S3 | USB host/device 両側の契約と異なる実装間の通信 | 他 controller/speed、PD |
| DUT と安価な peer | 指定通信契約の繰り返し検証 | 独立実装との相互運用の代替 |
| PD source と電圧計 | CC 交渉、VBUS、電源復旧 | USB データ通信 |

現構成の経路代表はLink＋V103、LinkE＋V307、ESP32＋UIAPduino V003、RP2350＋L103です。
P4の系列代表とは別に、配線済みのUARTとGPIO、製品HID、PPPSの復帰を明示実行します。
系列代表を共通 API の基準とし、小 RAM、別 ABI、複数 ADC/I2C、DAC、別 USB controller、製品 pin map など
実装差分の理由がある対象を追加します。build は生成対象全体、実機は代表と差分を確認し、
未検証 part/route は明示します。clock/baud/mode/buffer 境界の組合せは契約ごとに選びます。

## 環境分離

共有するのは論理信号名、必要機能、安全条件、一般的な許容差です。
個体 ID、port、実配線、電源操作先、計測器、実測補正値はローカル設定に置きます。
配線テンプレートは共有できますが、特定個体の識別子や実測値は含めません。

拡張時のfixture設定は、共有の安全な default → Git 管理外の *.local.toml → 環境変数 → CLI の順に解決する設計です。
現在の入口は --bench でlocal TOMLを明示し、未知キー、接続先、電源対象、GPIO配線を検証します。
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

host と device は両方を検証対象とし、CH32X035 と ESP32S3 の役割をボード名で固定しません。
DUT host / peer device と DUT device / peer host を対等な別ケースとして定義し、
それぞれの role 用 profile、契約、実行結果を分けます。一方の成功を他方の保証にしません。
ケースごとに board/firmware/driver/VBUS が指定 role を満たすことを確認します。
host と device の組を成立させ、同じ role 同士の接続を通信検証として扱いません。
P4 の書き込み・観測と USB host は別の役割です。

| 契約 | 観測と期待結果 |
|---|---|
| 列挙・control transfer | descriptor/configuration、request 応答、無効 request |
| CDC / bulk | 双方向の既知 payload、連番/長さ/CRC、packet・buffer 境界、short/zero-length |
| HID / interrupt | report descriptor/report の一致、入出力、採用する class request |
| 復旧 | bus reset、切断/再接続、stall/clear、peer reset 後の再通信 |
| コアとの共存 | timer/UART 等との同時動作 |

API/class の採用範囲が未確定なものは、契約確定後に実装対象へ加えます。
両側の制御ログは被測定 USB と別経路にします。両側を書き込み、READY 問合せを完了してから USB を有効にします。
primary が先に upload されるため、setup 直後の通信開始は peer upload 中の reset を誤検出します。
状態を再問合せ可能にし、開始応答の待受けで列挙ログを読み捨てないようにします。

VBUS の給電元、逆流防止、切断方法、GND は wiring と安全条件に含めます。
FS のペアで HS を保証しません。安価な CH32X035 peer に替えても contract ID と payload を維持します。
同じ stack 同士に共通の不具合があっても通るため、ESP32S3 等との独立相互運用検証は残します。

## プローブとの責務分担

| 保持先 | 保証対象 |
|---|---|
| このコア | Arduino CLI recipe、DUT upload/reset/monitor、HID、公開 API、経路別の最小 E2E |
| ch32rv / プローブ | transport/protocol、転送境界、設定永続化、flash/debug 内部処理、firmware update |
| plugin / 計測側 | fixture lifecycle、port/peer 解決、lock、capture、decoder |

プローブ内部の検証一式は複製しません。既存 tcp_link のようなケースは DUT E2E とプローブ単体に分解します。
具体的な移動は相手リポジトリの入口と対応させてから行います。通常テストからプローブを勝手に書き換えません。

## 作り直す依存順序

1. contract ID、capability/対象 matrix、設定 schema、安全条件を定義する。
2. Linux で隔離 build → 一意な DUT へ upload → READY → GPIO 外部観測 → 証拠保存を通す。
3. 共通 API と系列差分を分け、SoftWire/SoftSPI、独立 peer による Wire slave、ADC/PWM 設定、DAC の不足を埋める。
4. USB host/device 両側の実装範囲を定義し、role 別に独立 peer の契約を実装する。
5. 経路別 Arduino E2E、package、外部 library を追加し、旧ケースを契約単位で置換・撤去する。

完了条件は既存テストの温存ではなく、対象契約を観測でき、必要 target/経路で実行でき、
設定と証拠から別の Linux 環境で再現できることです。

## 参照

設備別分類と peer/初期化の方針は
[基礎ガイド](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/main/TESTING_BASICS.ja.md)、
[詳細ガイド](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/main/TESTING_ADVANCED.ja.md)、
[peer の例](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/tree/main/examples/12_peer_host_core)
を参考にしています。plugin の peer skip と、この計画の必須契約ゲートは別の層です。
