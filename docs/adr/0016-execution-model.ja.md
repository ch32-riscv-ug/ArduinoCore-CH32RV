# ADR-0016: 実行モデル（system は begin で自動、利用者のコードは書いた場所で動く）

- Status: Accepted

## 決定

コアごとに通常処理の流れを 1 本にし、task ごとの stack を持たない（ADR-0006 と同じ前提）。その上で、次の規則にする。

1. **ドライバの通常処理は system service とし、`begin()` が登録する。** `delay()` と `loop()` の戻りがそれを進める。
   利用者は `maintain()` のような呼出しを書かない。登録は各ドライバが持つ節（`ch32rv_service_t`）で、枠も容量も無い。
   登録しないスケッチには登録の仕組みがリンクされない。`end()` で外す。ただし、装置が無ければすぐ戻る service は
   外さなくてよい。USB デバイスは、`end()` を再起動（DFU の detach）のためにしか呼ばない小さな品種があるので、
   容量を優先して外さない（2026-10-28）。
   通知を配る節も同じ形で、配る関数はクラスが callback の登録のときに指す（仮想関数にすると、登録しないクラスでも
   リンクされる）。
2. **利用者のコードは、利用者が呼んだ場所でだけ動く。** 通知の callback と任意の Work は、明示の呼出し
   （`CH32RVTaskKit::Executor::runOnce()`、ライブラリの配達の呼出し）からだけ実行する。`delay()` からは呼ばない。
3. **例外は protocol の provider だけ。** 相手がタイムアウト付きで待っていて、利用者のコードがデータの出どころに
   なるもの（MSC の媒体など）は、専用の登録をしたときだけ system の中から同期で呼ぶ。通知は provider にしない。
4. **入れ子にしない。** system の巡回、Work の巡回、各ドライバの進行は、どれも自分の中から再び始まらない。
   照会（`ready()` など）は状態を読むだけにし、進行は service が行う。
5. **待たない部品を基本にする。** ISR と通常処理をつなぐ部品（CH32RVSyncKit）は 1 回試して戻る操作だけを持ち、
   blocking 版を持たない。ISR と共有するかは型で宣言する（`IsrSafe` / `LoopOnly`）。

時間の保証はしない。戻らない `loop()` や callback はすべてを止める。時間の決まった protocol の処理は IRQ と
hardware に置く。

## 理由

小さな系列（V003 の RAM 2 KB）で RTOS を常設できず、それでも USB・PD・Ethernet を「begin しただけで動く」
ようにしたい（呼び忘れで動かない、を無くす）。一方で、利用者の関数が `delay()` の途中で動くと、loop の途中に
割り込まれた状態を利用者が扱う必要が生じ、小さな板で学ぶ人には負担が大きい。そこで自動にするのは
ドライバの内部だけにし、利用者のコードの実行場所は利用者が書いた場所に限る。ESP32 や Pico の RTOS で学ぶ
「ISR は合図だけ、処理は通常側」「満杯は答えで、待つことではない」は、この規則のまま上位へ持ち越せる。

検討の経過（A / B / C / 別 stack の比較、レビュー 01〜05、決定）は planning/core-redesign/tasks/ARCH-SELECT-01 にある。

## 現在の実装範囲

- 1・4: `cores/arduino/ch32rv_service.{h,c}`。USB デバイス・USB ホスト・Network・USB PD が登録する（2026-10-22）。
- 5: `libraries/CH32RVSyncKit`。V003・V103・V307・X035・H417 で 10 kHz の割り込みとの実機試験を通した。
- 2 の Work: `libraries/CH32RVTaskKit`（任意）。
- 2 のライブラリの通知: `cores/arduino/ch32rv_deliver.{h,c}` の `deliverEvents()`。USB デバイスと USB ホストの
  通知 callback（`onRx()`、`onBusEvent()`、`onKeyboard()` など）はここからだけ呼ばれる。届くまでデータは
  クラスの buffer か相手側で待つ。Network ライブラリが USB の生フレームを受ける口は利用者のコードではないので、
  サービスの中で呼ぶ別の口（`CH32RVUsbDeviceNet::onStackFrame()`）にした。
- 3 の provider: MSC の `onRead()` / `onWrite()` / `onStartStop()`、CCID の `onApdu()` / `onEscape()`、
  Vendor の `onControlRequest()`、Video の `onFillPayload()`、DFU の `onDetach()`、USB PD Source の
  `onSetVoltage()` / `onVbus()` は、今までどおりサービスの中から呼ぶ。
