# 比較用の用途と刺激

版 P1-1。[実験仕様](experiment-spec.ja.md)に従う固定入力。
ここでの容量・周期・仮想の電圧等は比較用の設計値。製品の既定値や実機の成立を表すものではない。
P2 で trace と独立した参照モデルにし、候補の測定前に入力 hash を記録する。

## 共通の trace 規則

host は整数の仮想 tick を使い、標準は1 tick＝仮想1 ms。
同一 tick 内は外部入力、hardware 終端、通常処理の順とし、競合検査では入力と終端の順序を逆にした trace も作る。
通常の一巡は各 tick に一回。B／C の件数上限は8、同じ対象は一巡に一回、巡回位置を保存する。
用途で宛先 FIFO を使う場合の基準容量は4件。入力 queue と共用する実装は二重に数えず、別 queue なら両方を数える。
入力が増えない状態でも、受理済み処理と回収が落ち着くまで進める。停止判定は1000巡を検査用上限にし、超過を記録する。
この上限は製品 timeout や時間保証ではない。絶対期限を超えて故障させる trace は別に扱う。

内部 callback 回数の一致ではなく、外部からの受理・拒否・出力・最終状態と、その前後関係を照合する。
同じ結果が許される範囲を oracle に定義し、最速の候補の内部順序を全候補へ要求しない。
hardware 操作は受理 tick から指定した tick 後に完了する fake backend にし、通常処理が遅れても結果は保持する。

## 部品の容量と負荷

| 部品 | 基準・変化させる入力 | 期待する観測 |
|---|---|---|
| queue | 容量1、4、16。payload は uint8、uint32、16 byte の値。各型・容量を別 build | push N件成功、N+1件目拒否、N件を FIFO 回収。wrap 後も一致 |
| ring | 容量16、64、256 byte。0、1、N−1、N、N+1 byte の入出力 | 部分受理数、連続領域、wrap、順序。未確定領域を consumer が読まない |
| message buffer | 最大 payload 8／32 byte、総 storage 32／128 byte | 長さ metadata を含む実容量、全体受理か拒否。受理数を容量の商だけで決めない |
| flag | bit0、bit1 を両順序で set。any／all、読み取りと消費を別条件 | clear との競合で必要な通知が消えず、観察だけでは消費しない |
| semaphore | 上限1／4、初期0。release 上限+1回、tryAcquire 上限+1回 | 上限超過と空を判別。無言で wrap しない |
| lock | 通常処理、IRQ 共有、2 owner の競合 | try の結果、所有者以外の解放、再帰禁止。保護区間に dispatch を入れない |
| pool | block 16／64 byte、個数1／4 | 全貸出、満杯、順不同返却、二重返却、通知が残った block の再利用 |
| work／event | 対象1／4／8、1巡の上限1／4／8、宛先 FIFO 容量1／4 | 再投稿、巡回、公平性、容量・metadata・idle の費用 |

基本の容量不足試験は N+1件を consumer 実行前に投入する。
追加負荷は1 tick当たり1、4、16件、consumer は1 tick当たり1件を1000 tick繰り返す。
負荷は処理が追いつく条件と必ず満杯になる条件の両方を含む。拒否を「処理した」と数えない。
全通知に payload queue を使う必要はなく、集約可能な情報は同じ意味の状態として保持してよい。

## K0 V003 I2C Knob

用途の参考は [UC-03 の固定ソース](https://github.com/wagiminator/CH32V003-I2C-Knob/tree/5302daa8990ee850de18c963ade8968e4e976350)。
以下の K0 は新しい比較契約であり、上流と同一レジスタ仕様とはしない。

| 項目 | 固定条件 |
|---|---|
| 入力 | 論理的な signed 差分。frontend に int16 の飽和 accumulator と sticky overflow を持つ |
| 初期状態 | position＝0、step＝1、下限−1000、上限1000、設定 sequence＝0 |
| 設定 frame | little endian 8 byte。sequence uint16、step int16、下限 int16、上限 int16。step は1〜127、下限≤上限 |
| 受理容量 | 受信 staging 1件＋完成した未反映設定1件。未反映分を上書きしない |
| 公開値 | little endian 8 byte。position int32、適用済み sequence uint16、status uint8、予約0の uint8 |
| status | bit0 入力飽和、bit1 設定拒否、bit2 設定不正。reset まで sticky、予約 bit は0 |
| snapshot | 公開用と読み出し保持用を各8 byte。読み出し開始で保持し、STOP／repeated START で取引を終える |
| 取引 | 8 byte の書き込みを STOP で確定。途中の STOP／repeated START は不正 frame。読み出し超過 byte は0 |
| 通常進行 | 設定回収→検査と適用→差分回収→範囲へ飽和→公開値更新の順 |

設定 frame の不正は旧設定を維持し、適用済み sequence を更新しない。
満杯時は次の書き込み取引の受理を拒否する。I2C hardware でどの ACK/NACK に対応するかは K-HW の事前条件として記録する。
その対応が成立しない場合は host の受理検査だけ PASS にし、wire 契約を成立済みにしない。
公開値は読み出し中に変更しない。メインと IRQ の8 byte copy の保護、buffer の切替費用も集計する。

| trace | 刺激 | 期待結果 |
|---|---|---|
| K-NORMAL | t1に+3、t2に−1。t3以降に読み出す | 最終 position＝2、overflowなし |
| K-SNAPSHOT | 公開 position＝2から読み出し開始、4 byte目の後で+5と通常処理、残りを読む | 全8 byteは旧 snapshot。次の取引では position＝7 |
| K-CONFIG | 通常処理前にseq1 step2とseq2 step3を連続投入。seq1適用後にseq2を再送 | 先の1件だけ受理、後は拒否。再送後にseq2適用。設定の消失なし |
| K-INVALID | 7 byteでSTOP、step0、下限>上限を別取引で投入 | 旧設定を維持、statusで不正を判別 |
| K-SATURATE | consumeを止めて+20000を2回投入、その後consume | int16最大で飽和を記録。positionは有効上限に飽和 |
| K-RACE | 差分 take と通知消費の前後に+1を注入 | 重複せず最終 positionが1増え、未処理なら後の進行が残る |
| K-WRAP | sequenceを65535→0へ変える。generationを使う内部方式も小さい試験幅で周回 | sequenceの大小で新旧を誤判定しない。古い参照を新 object へ配達しない |

K-HW は同じ論理入力を外部 GPIO／I2C peer へ対応させ、100 kHz と400 kHzを別条件で試す。
part・端子・clock・peer がその速度を満たすことを資料で確認してから実行する。
通常負荷は論理差分100回/秒、読み出し100回/秒、設定1回/秒を60秒。
実入力の debounce・エンコーダ解釈の比較は別 profile にし、core の進行方式と混ぜない。
B2 は完成設定の容量を2件にし、その他の条件を維持する。

## X0 X035 PPS 操作画面

用途の参考は [UC-01 の固定ソース](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/tree/7eba86abd2467bb53f609189d8c3c7d75e4d6c00)。
上流の電力・積算表示等を全て X0 に含めた扱いにしない。U と X0 の機能差表を P2 で作成する。

| 項目 | 固定条件 |
|---|---|
| PD の用途状態 | desired／inFlight／confirmed とsession。requestは同時1件。confirmedの有効性を別に保持 |
| 入力 | 増減・適用の3操作。増減は20 mV、適用でその時点の値を要求対象へ確定。操作 queue容量4、1操作4 byte |
| 交渉中の再操作 | 次の適用値は最新1件へ集約。実行中の要求は変更しない。新要求は先の回収後 |
| 切断・失敗 | confirmed無効化、旧sessionの要求を回収。切断後は再適用操作を待つ。失敗も自動で無制限再試行しない |
| 計測 | 電圧・電流を100 ms周期で取得。失敗時は前値とstale表示。最新状態だけを画面へ渡す |
| 画面 | 128×64、1 bit。希望値・成立値・測定値・状態を表示。最大10 frame/秒。転送 payload は32 byteまで |
| 描画 | 同じ frame は同じ view revision。途中の変化は次frameへ。完了前のtx bufferは不変。全画面RAMは必須にしない |
| I2C | 表示と計測を共有。実行中1件、基準はbusy。待つownerをround-robinで再試行。画面はtransaction毎に解放 |
| 容量 | 表示tx payload32 byte＋controllerに必要なheader、計測結果payload8 byte。protocol内部bufferは別計上して全群で揃える |

表示の glyph、配置、数値表現、I2C command byte と header は P2 で共通 renderer と golden 出力として固定する。
独自の最小 glyph を作るか、由来を確認した同一素材を使用する。候補ごとに font を減らしてサイズを稼がない。
文字列表示だけの対照と高機能 framebuffer の候補を同じ描画機能として比べない。

host の fake PD は session1、能力範囲5000〜11000 mV、上限3000 mA、要求電流1000 mAとする。
要求は受理から5 tick後、I2Cは2 tick後に終端。これは protocol の実時間規定ではない。
初期 desired＝5000 mV、confirmed＝無効。正常交渉の結果が来るまで成立扱いしない。

| trace | 刺激 | 期待結果 |
|---|---|---|
| X-CHANGE | attach後に増加→適用で5020を要求。受理を観測後、完了前に増加→適用で5040を指定 | 5020の結果を回収後に5040を要求。送信中の値を書き換えず、最終confirmed＝5040 |
| X-DETACH | 要求の完了直前にdetach。完了→detachの逆順も検査 | confirmed無効、旧要求は一度だけ回収。session2 attachだけでは再要求しない |
| X-FAIL | 要求に失敗終端を返す。100巡進め、再適用操作を追加 | 操作なしで再要求を連打せず、再適用で次の要求を開始 |
| X-FULL | consumer停止中に増加、増加、増加、適用、減少の5操作を投入 | 最初の4件だけ受理、減少を拒否。希望値5060を適用し、拒否分を後から処理しない |
| X-DRAW | frame転送開始後にviewを2回変更 | 使用中bufferは不変。進行中frame完了後、最新revisionを描画し、間のrevisionは集約可能 |
| X-BUS | 描画中に計測を投入、さらにタッチ追加profileで入力要求 | transactionを壊さず全ownerが進む。完了通知が満杯でも結果を回収 |
| X-STALE | 計測を一度成功、その後失敗、さらに成功 | 前値＋stale→新値＋正常へ遷移。失敗した取得を新しい測定値にしない |
| X-DELAY | loopとcallbackに同じ長い仕事を置き、進行呼び出しの位置を変える | 自動参加・再入の契約どおりのtrace。遅延によるprotocol失敗は別に記録 |

入力列の「受理を観測後」等は明示的な barrier にする。候補の速さで故障刺激が別の状態へ当たらないようにする。
遅延・throughput を比較する試験では barrier を使わず、同じ外部時刻列を与える。二つの結果を分ける。

X-HW は PPS対応source、計測器、表示器、入力と配線を明示してから開始する。
要求範囲はsourceが示す能力、boardの許容範囲、負荷、計測器の共通範囲から固定し、hostの仮想値を無条件に送らない。
PD/CC traceを取得できない場合は「電圧・状態を観測した」とし、全protocol条件を確認した扱いにしない。
初期の通常負荷は計測10回/秒、画面最大10 frame/秒、操作2回/秒を60秒。
各候補で同一のsource・負荷・入力列・I2C速度を使い、必須protocol処理は適用操作がない間も進める。
B2 は同じバスにタッチ取得50回/秒、結果payload8 byteを追加。物理タッチ未準備ならfakeの結果を区別する。

## 実行機会と待機の対照

RUN-02 は次の profile を R/A/B/C の対応可能なものへ適用し、未対応も表に残す。

| profile | 進行する場所 |
|---|---|
| M | 明示run／stepのみ。delayは通常処理を進めない |
| S | Mに加えloop外とdelayでsystem範囲だけ進める |
| U | Sに加え明示参加したuser範囲を進める |

system／userを別枠にする実装は両枠のmetadataとstackを数える。
未参加user、初期化前、callback内、ISRからの呼び出しを含め、勝手な配達がないことも検査する。
非再入の基準ではcallback内のdelay(0)でuserを再配達しない。
systemを入れ子で進める案も、同じsystem関数の再入と所有を別契約にする。

待機は、try、状態を残して非同期再開、loopからの独立同期wrapperを比較する。
producerが通常処理の場合、wrapperが許可範囲をpumpできる条件だけ成功を期待する。
callbackからの禁止された待機は検査用エラーとし、timeoutで偶然抜けたことを成功にしない。

独立stackは EXP-03 の別対照として、同じ仮想I/O列を使う「入力待ち→要求→完了待ち→表示」を記述する。
最初に各stack・制御情報・idle等の配置を積算する。対象に収まらなければ容量不足を報告してよい。
実装可能なportの調査、切替・待機の実測はX035以上で成立する場合に限る。
2 stackの協調方式とtaskごとのstackを持つ方式を区別し、対象MCUに合うportがなければhostの値で代用しない。
この対照の結果だけで標準coreにRTOSを提供することは決めない。

## 保存と休止とコア間の刺激

| 対象 | 初期の固定刺激 | 成立条件 |
|---|---|---|
| 保存 | blob16 byte、keyはuint16を4個・各value uint32。初回、同値、100更新、満杯、再起動 | RAW／blob／keyの機能差を分離。容量・書込み量・読出し結果を照合 |
| 保存容量 | 確認済みの消去単位Eを1個／2個／4個。0予約の未使用構成も | 実際の予約byte、metadata、有効payload、更新可能回数と作業RAMを表示 |
| 保存故障 | 各program／eraseの前後と、媒体モデルが許す操作途中で停止 | 検出のみ、旧値or新値を保持、保証なし等の宣言と照合。1領域へ2領域と同じ復旧保証を要求しない |
| V006休止 | 通常10秒→休止要求→表示中cancel→wake。最終idle確認の前後にも入力 | I/O回収、通知保持、時刻の進行、復帰後再初期化。電流は実機で別測定 |
| 取得buffer | 16 bit×64 sampleのblockを2個、producer先行／consumer停止／返却順変更 | DMA等が所有するblockを描画側が再利用しない。物理sample rateは能力確認後に固定 |
| H417 | 一方向message16 byte×容量4、別条件で64 byte block×2の貸出と返却 | 連番・整合性、所有、片側停止／再起動時の回収。データ可視性は実機で確認 |

保存はnamespace・可変文字列キー・削除・型変更を追加機能として別に測る。
1領域が満杯になったとき、明示停止する方式と消去再保存する方式の動作差を表にする。
電断の注入はPC側切断だけでDUT電源断とみなさず、電圧と停止位置を独立観測する。
H417は単一コア、各コアの固定owner、共有実行器を分ける。実機なしのhost模型は所有契約の検査に限定する。

## 教材と変更課題

教材は周期入力、queue、非同期I2C、K0、X0、休止・保存の順に同じ意味を使う。
実行前に「次に進む処理」「再利用できるbuffer」「満杯時の結果」を予測し、コード変更後のtraceと照合する。

変更課題は K0の設定2件化、X0の表示交換・タッチ追加、queue容量削減、切断復旧追加の5件。
同じ課題・説明を各候補へ適用し、触ったファイル、追加状態、契約変更、誤解、支援回数、完了したかを記録する。
時間は補助指標にし、モデルの生成速度やモデル同士の自己採点を利用者の学習効果としない。
実利用者が試す順序と経験を残し、先に見た案への慣れも解釈に含める。
レビュー未実施の場合は未検証として P5 へ渡し、機械計測だけで教育効果を確定しない。
