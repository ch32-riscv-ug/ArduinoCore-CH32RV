# 全体構成の決定（P5 前半の結論）

2026-10-22。利用者の指示でこのセッション（実装担当、arduinocore-ch32rv-4b）がまとめた決定記録。
対象は提案版 4（[proposal](proposal.ja.md)、[usage](usage.ja.md)、be0ecea）とレビュー 01〜05。
**2026-10-22 に利用者が承認した（「この方針ですすめて」）。** 割り込み禁止区間などもサンプルで示す、という追加の指示付き。

## 決めること

提案版 4 を採用する。要点は次の 5 つ。

| # | 決定 | 根拠 |
|---|---|---|
| D-1 | **A を基本にする。** 利用者は共通部品を使って `loop()` から短い step を呼ぶ。Work／Executor（B）は任意部品として残すが、基本教材と下位 I/O の必須依存にはしない | 数値は A が小さく（K0／X0 で R と一致）、OEP（レビュー 03）は A を読みやすいと評価。B の便利さは仮説のまま |
| D-2 | **system は `begin()` で自動登録し、`delay()` と loop の戻りで進む。** PD の `maintain()` のような「呼び忘れると死ぬ」処理を無くす。進行は service に限り、照会（`ready()` 等）は観測だけ | 利用者の第一の希望（F10）。製品の USB／Ethernet はすでにこの形 |
| D-3 | **利用者の関数は、書いた場所でだけ動く。** 通知 callback（onRx 等）と user work は、明示の呼出し（`runtime.deliverUserOnce()`、`app.runOnce()`）からだけ実行する。`delay()` から利用者の通知を呼ばない | 「delay の途中で自分の関数が動く」を標準にしない（レビュー 03 U5）。失敗が見える形（buffer に残る）で起きる |
| D-4 | **protocol provider だけは例外。** MSC の媒体操作や動的な応答生成のように、相手がタイムアウト付きで待っていて利用者のコードがデータの出どころになる関数は、専用 API（`setSystemProvider` 等）で登録したときだけ system の中から同期で呼ぶ。通知は provider に昇格させない | レビュー 04 V1。これが無いと USB メモリの速さが loop の `delay()` で決まる |
| D-5 | **受理・所有・停止・回収を分ける（G3）、公開部品は try 操作が基本で blocking は標準に付けない（G4）、系列・休止・保存・H417・外部 RTOS は境界だけ今決める（G5）。** 内容は proposal 版 4 のとおり | レビュー 01〜05 で矛盾が残っていない |

採らないもの：RTOS／task ごとの stack の標準提供、preemption、汎用の event bus、blocking 既定の API、実行中にクロックを変える API、`ready()` の裏で進む poll。

## 優先順位（利用者の方針）

1. **小さくて速い。** V003（Flash 16 KB、RAM 2 KB）で問題なく使え、X035 の機能を全部使う構成でも邪魔にならない。使わない部品は 0 B（link で確認）。
2. **明示的。** 小さくても「誰がいつ呼ぶか」が読める。隠れた進行・隠れた配達を作らない。
3. **基礎的な組み込みをここで学び、ESP32 や Pico などの上位へ自然につながる。** 「長い処理を短く戻す」「受理は成功ではない」「通知とデータは別」「満杯と寿命を扱う」を、入門の例から完成例まで同じ規則で教える。ESP32SyncKit／ESP32TaskKit と同じ考え方（ISR は合図だけ、処理は通常側）。マルチコア・スレッド・RTOS は上位のマイコンで学ぶものとし、このコアでは教えない（H417 の 2 コアは境界だけ決め、教材の対象にしない）。
4. **ch32fun へ逃げられない軽さ。** ch32fun との互換は考えない。Arduino から入った人が「重いから ch32fun にしよう」と思ったら失敗。空のスケッチ・Blink・K0 の大きさを ch32fun の同等物と並べて測り、差を説明できる範囲に保つ。

## ESP32 の兄弟ライブラリとの対応

ESP32SyncKit／ESP32AutoTask／ESP32TaskKit を使った人が、同じ言葉で移れることを狙う。名前は意味が同じときだけ同じにし、意味が違うものに同じ名前を付けない。

| ESP32 側 | CH32 側（この決定） | 違い |
|---|---|---|
| ESP32SyncKit `Queue<T>`：`trySend`／`tryReceive`／`overwrite`／`count` | `Queue<T, N, Sharing>`：同じ名前の try 操作 | 容量はテンプレート引数（heap を使わない）。`send(timeout)` のような blocking 版は無い。ISR 共有かどうかは自動判定ではなく型で宣言する（V003 では IRQ 禁止区間になるので、費用を見える形にする） |
| `Notify`（カウンタ／ビット） | `Flag`／`EventBits`、`CountingSemaphore`：`trySet`／`tryTake`／`tryTakeAll` | `waitBits(timeout)` は無い。待つなら step の条件か、任意 Work の起動条件にする |
| `BinarySemaphore` | `BinarySemaphore`：`tryGive`／`tryTake` | 同上 |
| `Mutex`＋`LockGuard` | `CriticalSection`、短い `TryLock`、固定 `BlockPool` | task 待機者が無いので優先度継承・再帰・待機順は無い。lock を持ったまま callback をまたがない |
| ESP32AutoTask（弱シンボルの hook が周期で回る） | **system の自動進行**（begin で登録、delay と loop の戻りで進む）と、A の step | 利用者の関数は自動では回らない（D-3）。「書かなければ 0 B」は同じ |
| ESP32TaskKit（Task／startLoop／requestStop） | **任意の Work／Executor**：`attach`（成功で初回 ready）、`wakeOn*`、`runOnce()`、期限は `Deadline` と共通 | stack は持たない。work は状態を残して短く戻る。実行は明示 `runOnce()` のみ |
| — | `Deadline`／`Period`（単独部品） | ESP32 側の `vTaskDelayUntil` に相当する役を、executor 無しの比較だけで行う |
| ESP32TaskKit の examples 11（run-time stats） | 任意の実行統計（service／work ごとの回数・累計・観測最大、周期の取り逃し） | 「空の巡」はアイドルではない。有効にしなければ 0 B |

## 学習の導線

1. `loop()` に step を順に書く。I/O は `tryStart`→`tryTake`。満杯と Busy をそのまま返す。
2. `Queue`／`Flag` で ISR と通常処理をつなぐ（ISR は合図とデータだけ）。
3. `Deadline`／`Period` で周期と再試行を書く。
4. USB／PD／Ethernet を `begin()` するだけで裏で進むことを知る。通知 callback は `runtime.deliverUserOnce()` で受け取る。
5. 起動条件が増えてきたら、同じ step を `Work` に載せて `app.runOnce()` にまとめる（任意）。休止が要る用途でここが効く。
6. provider（MSC の媒体など）が要るときだけ、`setSystemProvider` と「delay の前に共有 state を整える」を学ぶ。

## いまのコアとの差

実行モデルは変わらない（1 本の stack、callback から再 dispatch しない、`delay()` と loop の戻りで system が進む）。変わるのは次だけ。

- system の進行を登録制にして PD も入れる（`maintain()` を書かなくてよくなる）。
- USB 等の通知 callback が `delay()` の中から直接呼ばれなくなり、`runtime.deliverUserOnce()` を呼んだときに届く（既存スケッチへの唯一の影響）。provider は今までどおり裏で呼ばれる。
- データ部品、期限部品、任意の Work／Executor、任意の診断を足す。使わなければ 0 B。
- I/O の受理・結果・回収の規則を明文化し、非同期 I2C 等を足す。既存の API は残す。

## 今決めない（後段）

- 具体的な API 名・型・時刻幅、登録 storage が固定配列か intrusive か（R2 で選ぶ）、診断の counter 幅と表示。
- 保存形式、休止 manager の構造、H417 の共有 executor、特定 RTOS の port。
- 教育効果の証明（別の読者による R1 と、初学者の評価）。

## 実装の順番（このセッションが担当）

製品コアへの反映は、公開 API を変えない順に進める。各段で V003 と X035 の空のスケッチとの差を測り、使わない構成で 0 B であることを link で確かめる。

| 段 | 内容 | 公開 API への影響 | 確認 |
|---|---|---|---|
| 1 | **system の自動登録。** PD の `begin()` が runtime に service を登録し、`delay()`／loop の戻りで進む。USB／Ethernet の weak 参照 3 本を同じ登録の仕組みに揃える。`maintain()` は残すが呼ばなくてよい | 無し（追加のみ） | X035 の PD 試験（`--case pd`）、H417 の eth。V003 の空のスケッチの差が 0 B |
| 2 | **データ部品と期限部品。** `Queue`／`Ring`／`MessageBuffer`／`Flag`／`BinarySemaphore`／`CountingSemaphore`／`CriticalSection`／`TryLock`／`BlockPool`／`Deadline`／`Period`。Arduino.h に依存しない単独ヘッダー。ISR 共有は型で宣言 | 無し（新規ライブラリ） | host の単体試験（既存の実験の oracle を流用）、V003 で ISR を重ねた実機試験（R3）、link の大きさ |
| 3 | **I/O の受理・結果・回収の契約（G3）を既存ドライバに当てる。** PD の `request`、I2C の非同期 transaction、USB クラスの provider／通知の区分。`tryStart`／`tryTake`、復旧（reset→静止確認→Aborted→take） | 追加が中心。USB クラスの callback が「通知」と「provider」に分かれ、通知は `runtime.deliverUserOnce()` 経由になる（**既存の onRx の挙動が変わる**。移行の注意を docs に書く） | X035 の usb-peer 系、PD、I2C の実機試験。R4（reset 後に DMA／IRQ が buffer に触れないこと） |
| 4 | **任意の Work／Executor。** `attach`／`wakeOn*`／`runOnce`／`wakeOnDeadline`。既存の B 試作を基に、使わなければ 0 B | 無し（新規） | K0 で work なし／あり、X0 の必要構成（R2） |
| 5 | **任意の診断。** 登録一覧、service／work の統計 | 無し（新規） | off／on の一対の大きさ（R2） |
| 6 | 教材と docs。導線のとおりに examples を並べ替える。ESP32 の兄弟ライブラリとの対応表を README に置く | — | R1（別の読者） |

段 1 と 2 はいまの契約のまま入れられるので、先に進める。段 3 の USB の変更は、段 1・2 が実機で通ってから。

## 既存の試作・結果の扱い

- planning/core-redesign 以下の実験コードと結果は、段 2〜5 の実装の出発点と検証の oracle として使う。データ部品の単独ヘッダー化（Arduino.h に依存しない）は、ch32fun 互換のためではなく、大きさを測りやすく依存を切るため。採用した数値は proposal 版 4 の表のまま（RV32E・LTO なしの logic probe で、完成品の値ではない）。
- 版 1〜3 と、採らなかった B 中心・user 自動配達の案は履歴として残す。再検討する条件は proposal の「強い反対理由」のとおり。

## 承認

- [x] 利用者が D-1〜D-5 と優先順位を確認した（2026-10-22）。
- [x] 実装の順番に異存がない。追加の指示: 割り込み禁止区間（`CriticalSection` 等）の使い方もサンプルで示す（段 2 の examples に入れる）。

承認後に Accepted ADR（実行モデル、所有と回収、公開部品、系列境界）へ反映し、この文書を P6 の依頼の入口にする。
実行モデルは [ADR-0016](../../../../docs/adr/0016-execution-model.ja.md) に反映した（2026-10-22）。段 1・2・4・5 は実装済み、段 3 は移行の段取り待ち。
