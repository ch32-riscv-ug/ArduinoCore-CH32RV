# 完了枠の予約と、明示的な共有busの割当て

2026-10-21。実装commit `d52d1b1`。[入力](../../completion-arbiter-inputs.json)、[数値・hash](completion-arbiter-measurements.json)、[最終20 ELF](completion-arbiter-size-matrix.csv)を保存した。
既存のfake [IoSlot](../EXP-01/io-preparation.ja.md)を使い、受理時の完了枠予約とbus dispatchを接続した。
製品コア・USB・実I2C driverは変更していない。公開方式の採用はP5へ渡す。

## 比較する保存場所

| 条件 | 利用者数 | 完了record数 | 同じ利用者の未回収要求 |
|---|---:|---:|---|
| dedicated | 3 | 利用者ごとに1、計3 | 1件まで |
| pooled | 3 | 全体で共有2 | 1件まで |

各利用者のbufferは32 byte、計3個を別に持つ。
submitでrecordと結果の保存場所を確保し、同時にbufferを借用する。
queued段階ではまだbusを使わないが、利用者からbufferをread／writeできない。
recordがなければBusyを返し、受理した扱いにしない。poolはqueued・running・terminal未回収を合わせた2 recordの予算。

dispatchはqueuedな利用者をcursorからround-robinで探し、1回に最大1件だけRunningにする。
受理順のFIFOとは異なり、同じ利用者の要求列も持たない。
Runningの間は次を割り当てず、handler・delay・taskを裏で実行することもない。

## 取消・終端・返却

| 操作／状態 | bus | record・buffer |
|---|---|---|
| submit成功 | まだ割り当てない | 予約・借用済み |
| dispatch成功 | 1 recordへ割当て | 保持 |
| queuedのcancel／end | hardwareへ渡していない | localなCancelled結果を保存し保持 |
| runningのcancel／end | 停止確認まで保持 | 保持 |
| 停止確認付きfinish | 次のdispatchが可能 | terminal結果とbufferを保持 |
| noticeのack | 変わらない | 保持 |
| take | 自動dispatchしない | 結果をコピーしてrecordとbufferを返却 |

runningのfinishは、正しいtoken・有効な結果／prefix・停止宣言を要求し、宣言falseでは終端化しない。
取消要求は停止の証拠ではなく、成功が取消に勝つ場合も許容する。
queued取消がlocalに終端できるのは、producerへ一度も渡していないという制御上の条件による。
endは新規受理と通知を閉じるが、未回収を捨てず、回収前の再openも拒否する。

tokenはrecord index・generation・clientを照合し、同じslotの再利用や別clientへ古い操作を適用しない。
generationは非周回で、上限のslotは退役する。tokenは同じArbiter instanceの生存期間内の識別子。
buffer・record・Arbiterを回収まで生存させ、destructorで自動停止・回収はしない。
この試作は直列の通常文脈だけを対象にし、ISR／thread共有は提供しない。

## 公平性は2段階に分ける

受理済みのqueued要求では、回収した利用者がすぐ再submitしても、待っている別利用者を飛び越さずdispatchする。
dedicatedの3利用者を常時readyにした例では、割当てが0→1→2→0→1→2となった。

一方、pooledのtry-submitには受理機会の予約がない。
client0／1が回収直後に2枠を取り直す入力では、client2が12回続けてBusyとなる反例を確認した。
busのround-robinだけで、まだ受理されていない利用者の公平性までは保証できない。
受理待ちintent・quota・外側の呼出し順などの追加条件を比較する余地がある。
進行機会やhardware停止が得られる時間上限も置いていない。

## Hostの証拠

ASan／UBSan・警告errorでnative22件＋workspace規則3件、計25件が通過した。conftest.pyは追加していない。
独立したPythonのowner list・buffer台帳と、全22 trace・8,590操作を照合した。

- dispatch前の完了枠とbuffer借用、満杯拒否、別要求による同じbufferの二重借用拒否。
- 終端未回収のrecordを保持しつつ、別recordへbusを渡すこと。通知ackだけでは次の同client要求を受理しない。
- queued取消、running取消と停止宣言、end後の通知抑止・再open拒否・明示cleanup。
- 不正client／token／結果／prefix、成功の不足byte、停止宣言false、取消なしのCancelledを拒否し、出力を保持。
- 受理済み要求の巡回順と、共有枠に対する新規受理の偏りを別に検査。
- dedicatedは同clientの255件、pooledは2 record合計510件の8 bit退役を確認し、古いack／takeを拒否。
- 4 seed×600生成選択×2方式。途中の消費停止・通知ack・再投稿・失敗を含め、最後に全recordとbufferを明示回収。

fake IoSlotのfill／hashを使う模型であり、実busのbyte到達やDMA停止は検査していない。
EventExecutorへの配達、満杯mailboxからのcallbackも、この単位では接続していない。

## 初期状態の表現と費用

初回は「busなし」をrecord数で表したため、非zeroの初期値を含む管理object全体が`.data`に入った。
内部だけを「zeroならなし、その他はindex＋1」へ変更し、zero初期化可能にした。
publicなcurrent観測は従来どおりrecord数をnoneとして返す。
保存した22 traceを変更前のbinaryでも再生し、全stdoutが一致した。

V003／X035、GCC14.3.0、`-Os`、LTO off／onで変更前20 ELFと変更後20 ELFを保存した。
objectサイズは不変で、最終構成の`.data`は0 Bになった。
初期値imageと配置paddingを減らした結果であり、実startup時間は測定していない。

| 最終object | dedicated | pooled |
|---|---:|---:|
| Arbiter本体 B | 116 | 80 |
| 保護buffer3個 B | 108 | 108 |
| 入口の操作・引数・出力等 B | 89 | 89 |
| alignment込みstatic RAM B | 316 | 280 |

bufferには96 Bのpayloadと3本の借用owner pointerを含む。
測定対象は共通Record実装と入口で、専用metadataへの圧縮は今後の比較対象。
poolはrecord1個分の36 Bを減らすが、受理容量と利用者ごとの予約条件も変わる。
同等の保存保証・最大同時要求数を持つ方式同士の比較とはしない。

LTO onのimage B。入口と必要なsupport／libgccも含む。

| profile | dedicated変更前→後 | pooled変更前→後 |
|---|---|---|
| V003 | 1,984→1,880 | 1,960→1,896 |
| X035 | 1,768→1,664 | 1,728→1,672 |

RAMの少ないpoolが、必ずcodeも小さいわけではなかった。
未使用8条件は追加RAM・object symbolが0 Bで、image hashも各emptyと一致。
全最終20 ELFにallocator symbol・constructor arrayはない。
単独frameは最終使用条件でV003 36／40 B、X035 80 B。call chain／IRQ込みのpeakではない。
実startup・実I/O・IRQ backendを持たない構造probeで、CPU時間やbus bandwidthの結果ではない。

## 次の接続と再現

実I2C Masterはterminal後もtakeまで自身のrecordを保持する。
このfake Arbiterの条件を接続するには、backend側の結果回収と、利用側の完了record・buffer借用を分けるadapterが必要。
この比較だけで既存Masterへ次の要求をそのまま流せると決めない。
EventExecutorとの接続、受理公平性、backend引渡し、IRQ・完了枠の保護、用途の同一入力比較は残る。

```sh
EXP03_COMPLETION_EVIDENCE=work/completion-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_arbiter.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_completion_arbiter.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/completion-abi-new
```

保存archiveは`work/exp03-completion-20261021-evidence.tar.gz`。
変更前後のsource・compiler／binary hash・全traceと旧binary再生・JUnit・40 ELFとmap／disasm／stack情報・analysisを含む。
hash・byte数は測定JSONを参照する。出力先には新しいdirectoryを使う。
教材では「busが空いているのに受理できない理由」「通知ackとtake」「dispatchと受理の公平性」「zero初期値とFlash」を予想する。
人による教材実施・理解度レビューは未確認。

後続の[I2C回収分離](../EXP-01/i2c-handoff-result.ja.md)は、register Masterを使う別経路でnative70件・ABI20を追加した。
新Managerが外側recordとViewを保持し、backend take後も利用者のbuffer借用を維持する。資源台帳・event・IRQとの結合は残る。
