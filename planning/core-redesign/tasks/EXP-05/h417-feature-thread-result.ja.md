# 機能を除去した所有型のhost並列検査

2026-10-21。[message／block機能除去](h417-features-result.ja.md)の新headerを、
[既存の2 thread試験](h417-thread-result.ja.md)と同じworkloadへ接続した。
実装commitは`4d5942c`。[入力](../../h417-feature-thread-inputs.json)と[全実行・hash](h417-feature-thread-measurements.json)を保存した。
製品コア・USB・Flash、所有header、以前のdriverとABI入口は変更していない。方式の採用はP5へ渡す。

## 接続方法と比較条件

既存thread driverから派生したtranslation unitをtest fixtureが生成する。
変更はinclude・namespace・Fabricの型引数・disabled workloadの件数・gate解放後の予約確認に限定し、
thread loop、payload pattern、回収ticket、FIFO・重複・欠落のassertionを共有する。
置換対象の個数を照合し、基準driverが変われば無言で別の試験へ進まず失敗する。
生成したC++全文とhashを証拠へ保存する。

4機能構成×mutex／atomic gate×fixed／shared-access×4 seedで64実行。
各producerは、有効な機能についてmessage1,200件と64 byte block160個を生成する。
disabled側は0件とし、無限retryで無効な仕事を待たせない。
none条件では2 threadの起動・joinを行うがpayload操作を呼ばず、gate取得attemptが0であることを確認する。

各機能構成×2 gateのBusy試験8実行と、blockがある2構成×2 gate×2所有条件の回復試験8実行も行う。
message-onlyの並列回復handshakeを試した扱いにはせず、回復中の受理閉鎖・静止maskの全構成比較は先の直列検査を参照する。

## 結果

ASan／UBSanはnative80件＋workspace規則3件の83件、別buildのThreadSanitizerはnative80件が通過。
最終実行はすべて終了0、sanitizer診断出力なし。conftest.pyは追加していない。

sanitizerごとのstress集計。mutex／atomic・fixed／shared-access・4 seedの合計。

| 機能 | stress実行数 | message回収 | block返却 | 別threadがgate保持中の失敗取得回数 |
|---|---:|---:|---:|---:|
| none | 16 | 0 | 0 | 4 |
| messageのみ | 16 | 28,800 | 0 | 6 |
| blockのみ | 16 | 0 | 3,840 | 10 |
| 両方 | 16 | 28,800 | 3,840 | 12 |

両sanitizerで合計115,200 messageと15,360 blockを回収した。回復試験の新しい64 byte確認はこの集計に含めない。
messageは全word、blockは全64 byteのpatternを検査する。
gate取得時のticket順で各producerのsequenceを照合し、1件ずつのFIFO、欠落・重複なしを確認した。
全stressのjoin後に未回収message・Ready・借用が残らないことも確認した。並列中にobserverは呼ばない。

Busy試験では全12操作が拒否され、出力が変わらないことを確認する。
そのうち実際にgateを試す操作は、共通制御4個、message2個、block6個。
disabledメソッドはgateを取得せず拒否するため、実測失敗回数が表の4／6／10／12となる。
解放後はblockの有無に対応した予約結果と、通知ackによるgateの再利用を確認した。

block回復試験は、WritingとReadingのworkerを既知の停止位置で止めてから入口を閉じ、旧呼出しが戻ったことを確認する。
producer／consumerの静止宣言だけではrestartできず、DMA等のwriterの宣言を加えてepoch2へ進む。
再利用後に旧呼出し5個を拒否し、新しい64 byteを正常に回収する。
これは静止の宣言をhostで接続した試験であり、実DMAや任意の命令での片側resetを再現していない。

最初の実行は両sanitizerで4件ずつ失敗した。
旧Busy harnessがgate解放後にblock予約成功を無条件でassertしており、none／message-onlyの正しい拒否で止まったため。
派生harnessの確認だけを機能構成に合わせて修正し、所有headerは変更しなかった。
初回の失敗binary・source・JUnitも保存し、最終通過の数値と分けて記録する。

## 費用と成立範囲

機能除去の112 ELFについて、header・入口・builder・support・profile等の全入力hashが一致した。
新しいMCU実装を加えていないため、以前の16／84／168／236 Bと未使用48条件の0 Bを引き継ぎ、ABIを再測定していない。

標準C++ mutex／atomic同期で新しいfeature型を2 threadから利用できた証拠である。
H417 SRAM／MMIOの順序・IPC／HSEM・cache・DMA可視性や共有実行器の証拠ではない。
attempt／Busyの回数はsanitizerと検査器のatomic counter、OS schedulingも含み、性能順位・公平性・最悪時間には使わない。
有限のstressとTSan成功で全interleavingの正しさを証明した扱いにしない。

## 再現と残る作業

```sh
EXP05_H417_FEATURE_THREAD_EVIDENCE=work/h417-feature-thread-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_feature_threads.py tests/unit/test_workspace.py -q

EXP05_H417_FEATURE_THREAD_SANITIZER=thread \
EXP05_H417_FEATURE_THREAD_EVIDENCE=work/h417-feature-thread-tsan-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_feature_threads.py -q
```

TSanは別buildで実行する。出力先は新しいdirectoryを指定する。
保存archiveは`work/exp05-h417-feature-thread-20261021-evidence.tar.gz`。
原sourceと派生source、compiler・binary・動的libraryのhash、compile command、全実行結果、JUnit、初回失敗とanalysisを含む。
archiveのhash・byte数は測定JSONを参照する。

残るP4作業は、実行器への接続、固定ownerと共有実行器の比較、H417両imageの共有layout・起動handshake・実backend・DMA・片側reset。
教材では「disabledなら呼出しを消すだけでよいか」「Busyでも出力を保持する理由」「回収順と処理完了順の違い」を予想してから結果を読む。
人による教材実施・理解度レビューは未確認。
