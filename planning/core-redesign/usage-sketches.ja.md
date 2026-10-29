# P0 利用例と状態遷移の設計

版 P0-1、2026-10-19。[全体設計案](design-draft.ja.md)と一組の比較用資料。
以下の C++ 風コードは未実装 API の疑似コードであり、コンパイル済み examples ではない。
名前、object 配置、通知の接続方法は未確定。既存プロジェクトのコードを移植したものでもない。
用途の出典と固定 commit は [TODO から移した用途調査](todo-carried.ja.md#既存プロジェクトからの用途比較実験候補)に記録している。

## 比較で揃えるもの

A 明示 step、B 短い work、C 宛先 event で、用途の状態と外部から観測する結果を揃える。
通知方式や内部の呼び出し順まで同じにする必要はない。入力条件と許容する遅延・失敗の扱いは P1 で定める。
既存の直接実装とも比較し、同等の容量・失敗処理を持たない実装は、その差を記録する。

| 用途 | 揃える機能 | 方式に応じて変える部分 |
|---|---|---|
| V003 I2C Knob | 入力状態、設定書き込み、整合したレジスタ読み出し、満杯・不正値の扱い | 入力・設定の反映をいつ呼ぶか、ISR から通常処理への通知 |
| X035 PPS 操作画面 | PD sink、PPS 設定、計測、表示、入力、切断・失敗・再接続 | 通常処理の進行、通知、時刻条件、同一 I2C バスの調停 |
| V006 休止例 | 入力・時刻・表示、休止・復帰、保存の完了確認 | 休止条件を明示的に集めるか、任意の共通部品へ提示するか |

これらは機能契約の案で、各チップ上で達成できたという結果ではない。
IRQ へ任せる処理、必要な速度、外部プロトコル条件は一次資料と実験で詰める。

## V003：小さい I2C 入力機器

入力をホストへ公開し、ホストから設定を書き換える用途を使う。
コアの責務は I2C slave の取引境界、共有データの整合性、設定受け渡しを提供するところまで。
ノブの単位、上下限、設定の意味は用途側に残す。

### 保持する状態と所有

| 状態 | 更新する側 | 読む側・保持期間 |
|---|---|---|
| 入力差分・現在レベル・overflow | 入力 frontend の IRQ または選んだ取得処理 | 通常処理が競合しない take 操作で回収 |
| 受信途中の設定フレーム | I2C frontend | 取引完了後に検査。途中受信を有効な設定として公開しない |
| 完成した未反映設定 | frontend から一度だけ受け渡す | 通常処理が回収するまで上書きしない |
| 有効設定・論理位置 | 通常処理 | 次の入力反映と公開値の生成に利用 |
| 公開レジスタの snapshot | 通常処理が更新候補を作る | I2C の一回の読み出し中は同じ snapshot を保持 |

候補の最小形では、完成した未反映設定を1件だけ保持する。満杯中の次の書き込みは明示的に拒否する。
具体的にどの byte を NACK にできるか、ホストがいつ受理結果を読めるかは、I2C frontend とホスト側契約を P1 で定義する。
受理した設定が後の検査で不正だった場合は、設定結果の状態から判別できるようにする。
通信が ACK されたことと、設定が適用されたことを区別する。

読み出し開始で snapshot を固定し、STOP または定義した取引終了条件で解放する。
repeated START の扱い、公開 buffer の数、読み出しが続く間の更新を保留するかは比較条件にする。
複数 byte の値は単なる volatile 変数の共有で整合した扱いにしない。

### 用途側の一回分

```cpp
// 各 take は競合を処理した部品の操作。判定後に別途 flag を消す形にしない。
void Knob::step() {
    if (auto config = incomingConfig.take()) {
        configResult = validateAndApply(config, settings);
    }
    auto input = inputDelta.take();
    updatePosition(input, settings, position); // 飽和・overflow の方針は用途側
    registers.offerLatest(makeSnapshot(position, configResult, input.overflow));
    // 読み出し中の buffer は書き換えない。公開待ちは最新値へ集約できる。
}
```

入力が速すぎる場合に差分を飽和させるのか全変化を queue に残すのかは、用途の精度と RAM の比較になる。
debounce が必要なら別の短い状態処理と時刻条件にする。IRQ 取得でも入力を無制限に保持できるとはしない。

| A | B | C |
|---|---|---|
| loop から frontend の進行と `knob.step()` を呼ぶ | 初回・入力・設定完了で Knob work を ready にする | Knob 宛ての Reconcile 通知で `step()` を呼ぶ |
| 次の loop まで状態を保持する | 通知は集約し、データは上記の状態に保持する | 全件必要な設定と集約可能な起動通知を分ける |
| 時刻条件があれば step で判定する | 時刻条件があれば再起動を予約する | 時刻条件があれば宛先へ通知する |

B／C の ready 消費とデータ回収の間に追加入力が来ても、次回起動が残る手順を使う。
初回実行、通知登録前から存在する状態、公開 buffer の解放も必要に応じて再実行条件にする。
タイマー機能や汎用 payload queue がこの例に不要なら、その費用を含めない構成も測る。

## X035：PPS 設定・計測・画面・入力

主な流れは「入力 → 希望設定 → PD 交渉 → 成立結果 → 計測と画面」。
画面転送、入力、PD 交渉が同時に進行中になれる構成を記述する。
PD の protocol 進行と、用途が PPS 設定を変更する処理は分ける。
PPS 設定の変更がない間にも必要な protocol 処理を、UI 操作の発生だけに依存させない。

### 三つの設定と接続の状態

| 状態 | 意味 |
|---|---|
| desired | 利用者が希望する値と変更 revision。表示・入力で更新可能 |
| inFlight | 受理済みの要求、送った値、revision、接続 session。完了まで保持 |
| confirmed | 同じ session で成立を確認した値。有効／無効を別に持つ |
| connection | 接続の有無、session、利用可能な能力、その更新状態 |
| retryCondition | 新しい操作・接続状態・再試行期限等、再び要求してよい条件 |

交渉中に desired が変わっても、inFlight の値は変えない。
画面には希望値と成立値を区別して表示し、要求が受理されただけでは成立値にしない。
切断で confirmed は無効になる。再接続時は能力を再確認してから desired を評価する。
再接続で自動再要求するか、利用者確認を必要とするかは用途の仕様として P1 に残す。

### PPS 制御の一回分

```cpp
void PpsControl::step() {
    auto link = pd.connectionSnapshot();
    invalidateConfirmationIfSessionChanged(link);

    if (inFlight) {
        if (!sameLiveSession(link, inFlight.session))
            pd.requestCancel(inFlight.request); // 冪等。ここでは buffer を解放しない

        if (auto result = pd.takeTerminal(inFlight.request)) {
            // takeTerminal は hardware の参照が終わり、残る通知の参照も安全な結果だけを回収する。
            auto completed = inFlight;
            inFlight.clear();
            if (sameLiveSession(link, completed.session))
                recordOutcome(result, completed.sentValue, completed.revision);
            updateRetryCondition(result, completed, link);
        }
    }

    if (!inFlight && shouldRequest(desired, confirmed, link, retryCondition)) {
        auto proposal = validateAgainstCapabilities(desired, link);
        auto accepted = pd.tryStart(proposal, link.session, requestStorage);
        if (accepted)
            inFlight.remember(accepted, proposal, desired.revision, link.session);
        else
            rememberRejectionAndNextTrigger(accepted.reason);
    }
}
```

`shouldRequest` は、同じ値の成功後に毎回再要求せず、失敗後も無条件に再試行しない条件を含む。
`tryStart` は指定 session が有効なことを受理時にも確認し、必要な値を継続利用できる requestStorage へ保持する。
局所変数 proposal への参照を、関数から戻った後まで使用する意味にはしない。
busy の場合は資源解放通知、または定義した再確認時刻を待つ。ready にし続けるだけの無制限再投稿は避ける。
切断と完了の競合は protocol 側の状態順序と session に結び付ける。回収時に同じ接続だと判断しても、
その後の切断通知で confirmed を無効にする。古い要求の成功を新しい接続へ転用しない。
session 番号の周回と object の寿命も別途定義する。
回収後にも通知が残る案では、通知が破棄済み object を参照しない ID 表現と世代照合、
または通知の排出完了を回収条件にする方式を比較する。

### 表示・計測・入力

画面は、最新の view state と転送用 buffer を分ける。転送を開始した buffer は完了回収まで変更しない。
入力が増えても画面更新は集約できるが、差分操作を最新の一つへ上書きすると意味が変わるため、
入力差分の加算または有限 queue を使い、飽和・満杯を用途へ通知する。

```cpp
void Screen::step() {
    if (transfer) {
        if (auto result = bus.takeTerminal(transfer)) {
            transfer.clear();
            finishChunkOrRecordFailure(result); // 全 chunk 成功で描画済み revision を進める
        }
    }
    if (!transfer && needsNextChunk(view.revision)) {
        prepareNextChunk(view, frameState, txBuffer); // 一回の描画量も制限。準備だけでは進捗を確定しない
        transfer = bus.trySubmit(displayAddress, txBuffer);
        // busy なら内容と状態を保持し、資源解放・再確認時刻で再度試す。
    }
}
```

複数 chunk を使う場合は、画面全体を同じ revision で描く方式と、途中で最新内容へ切り替える方式を明示して比較する。
どちらも DMA／I2C が使用中の buffer の寿命は同じ規則にする。
計測も request と結果の状態を持ち、失敗時は古い測定値に鮮度情報を付ける。新しい正常値のように表示しない。

同じ I2C バスへタッチ入力を追加する変更課題では、表示を適切な transaction に分けてバスを解放する。
busy を扱う要求元同士の順序と、固定容量 arbiter の公平性を比較する。
優先入力を設定しても、実行中 transaction の途中へ別 transaction を割り込ませる保証にはしない。

### A：呼び出し順が見える構成

```cpp
void loop() {
    protocol.step();  // IRQ と分担した通常文脈の処理を進める
    bus.step();       // hardware のみで進む backend では不要な呼び出しにもできる
    input.step();
    pps.step();
    measurement.step();
    screen.step();
    otherWork.step();
}
```

全 step は一回分で戻り、周期・再試行を各状態から判断する。
長い処理を追加する際は、その処理を分割するか、必要な明示 step 群を呼ぶ。
system の自動 pump を比較する場合も、どの関数が進むかを定義する。ユーザー処理まで暗黙に登録しない。

### B：状態変化で短い仕事を起動する構成

```cpp
// 永続する work object と、起動理由に対応する接続を準備する疑似記法。
void setup() {
    bindState(input.changed, ppsWork);          // desired の更新後に ready
    bindState(pd.connectionChanged, ppsWork);
    bindState(pd.requestFinished, ppsWork);
    bindState(view.changed, screenWork);
    bindState(bus.requestFinished, ioOwners); // 完了した要求の owner だけへ
    bindState(bus.available, waitingClients);
    activateAndReconcile();                  // 登録前の状態と初回処理を取り込む
}

void loop() {
    domain.runTurn(); // 対象・件数の上限を持つ一巡。仕事は上記 step と同じ状態を扱う
    otherWork.step();
}
```

input、protocol、計測の周期等にも、IRQ・状態・期限に応じた起動条件が必要になる。
この記法は大きな汎用購読表を要求するものではない。静的な接続、関数と context 等の費用を比較する。
明示 run を loop 外の hook に移す案、delay で同じ domain を進める案を、それぞれ分離した条件で比較する。
どの案でも利用者が仕事を登録しただけで自動実行まで許可した扱いにするかは、明示的に選ぶ。

### C：宛先を持つイベントで進める構成

```cpp
void PpsActor::onEvent(Event e) {
    switch (e.kind) {
    case Reconcile: control.step(); break; // 最新状態を確認する集約可能な通知
    case Command:   applyCommand(e.command); control.step(); break; // 受理した順序を保つ
    }
}

void loop() {
    dispatcher.runTurn();
    otherWork.step();
}
```

宛先と型の宣言、有限 mailbox、payload の寿命、受理失敗を追加で記述する。
通常の command が満杯でも完了を失わないように、結果状態と永続する pending bit を別に確認する方式、
または要求受理時に完了用の枠を確保する方式を使う。単に「完了 event の送信失敗を無視する」ことは認めない。
pending bit の走査なら走査費用、予約なら予約容量を測定へ含める。
複数購読者へ同じ event を配る仕組みは、必要な変更課題を追加した場合に費用を測る。

## 三案に共通して確認する trace

以下は期待動作の案で、実行済みの検査結果ではない。

| 刺激 | 観測したい動作 | 主に確認する契約 |
|---|---|---|
| Knob の読み出し中に入力が変化 | 読み出し途中の値は一貫し、後の読み出しで新しい状態を取得 | snapshot と buffer の所有 |
| 設定の未反映中に次の書き込み | 受理容量を超える分は区別でき、先の設定を失わない | 有限容量、ACK と適用の違い |
| PPS 交渉中に希望値を変更 | 先の結果を回収してから必要な次の要求。画面に未成立の値を成立値として出さない | desired／inFlight／confirmed |
| 完了の直前・直後に切断 | 古い session の成立を残さず、要求と buffer は一度だけ回収 | 競合、cancel、寿命 |
| 描画中にタッチと計測が要求 | transaction の境界で他要求へ進む。満杯は各 owner が扱う | バス所有と公平性 |
| handler 中に再通知 | 現在の handler へ再帰せず、後の一巡に仕事を保持 | 再通知、深さ、一巡の境界 |
| 長い loop 中に delay(0) | 選択した自動進行案の範囲で実行機会を提供 | 自動参加の意味 |
| 長い callback 中に delay(0) | 再入を抑止する案では別のユーザー callback が進まない | 単一 stack の制約と教材 |
| user 用 mailbox が満杯 | system 用枠の予約だけでは長い処理の遅延を防げないことも確認 | 容量分離と時刻保証の違い |
| 表示中に休止または保存を要求 | 完了・取消・実行条件を確認し、使用中 buffer を破棄しない | sleep／Flash と進行の相互作用 |

## V006 と H417 へ広げる変更課題

V006 は入力・時刻・表示の状態機械を保ち、休止要求を追加する。
`準備 → I/O 回収 → wake 設定 → 休止 → 復帰 → 再照合` の各状態を置き、
準備中の新規入力で休止を取り消す条件、期限を越えて復帰した周期処理の扱いを記述する。
設定保存を加える場合は、保存受理直後に電源を落としてよいという意味にしない。
A は用途の step が条件を集め、B／C は同じ条件を状態と通知で確認する。

H417 は PD や描画を必ず別コアへ移す前提にせず、計算結果を渡す小さな接続例から境界を検査する。
片側は buffer の owner、もう片側は借用側とし、結果通知と返却の両方が必要になる。
共有 memory の配置・可視性、片側 reset 時の回収を定めるまで、単一コアの ring をそのままコア間へ使わない。
これは同一 API で全て解けるという主張ではなく、共通契約で残せる範囲を見分ける変更課題である。

## 資源と学習の評価へ渡すもの

概念上の RAM を `用途の状態 + 要求・転送 buffer + queue の要素領域 + 管理情報 + stack + 任意 heap` に分ける。
union、共有、配置の padding を含め、最終値は linker map と実行時観測で照合する。
queue の要素領域は容量と要素サイズの積から見積もれるが、それだけを総費用とは呼ばない。
候補固有の metadata を省いて同じ RAM と比較しない。ISR と通常実行の stack 利用も集計する。

P1 で容量と入力列を固定し、P2〜P4 では次の材料を集める。

- 最小構成、各部品の追加分、未使用の除去、完成用途の Flash・静的 RAM・観測 stack。
- idle 中と入力集中時の処理費用、IRQ 禁止区間、取りこぼし、通知から実行までの観測遅延。
- 表示器の交換、タッチ追加、容量削減、キャンセル追加で変更するファイル・状態・契約。
- 利用者が実行順、buffer の再利用時点、満杯時の結果を予測できるか。説明と変更課題への反応。

遅延の観測値を実行時刻の保証へ読み替えない。見積りを測定値として埋めない。
教材は同じ機能を少しずつ組み合わせるが、製品仕様の A → B → C 移行を意味しない。
