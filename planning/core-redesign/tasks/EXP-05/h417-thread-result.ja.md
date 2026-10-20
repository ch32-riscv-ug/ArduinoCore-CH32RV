# H417向け所有契約：hostの2 thread同期

2026-10-21。[直列の所有・回復](h417-ownership-result.ja.md)で使った`h417_ownership.hpp`を変更せず、
hostで実際の2 threadと排他gateを接続した。実装commitは`e0962f5`。
[入力](../../h417-thread-inputs.json)と[測定・入力hash・全実行結果](h417-thread-measurements.json)を保存した。
製品コア・USB・Flashは未変更。H417 backendや共有実行器を実装したものではなく、方式の採用はP5へ渡す。

## 接続した条件

gateは`std::mutex::try_lock`と、`std::atomic_flag`のacquire取得／release解放の2種類。
部品側は1回試すだけで、Busyや満杯・空へのretryと`std::this_thread::yield()`はhost driver側で行う。
thread数は2。fixedではcore0役がproducer、core1役がconsumer。
sharedでは両threadがproducerとconsumerを兼ねるが、借用中のblockは取得したcore役だけが操作する。
messageとblockを同じFabric上で同時に進め、通知bitのackも並行して試す。

各producerは16 byte message 1,200個と64 byte block 160個を順に生成する。
blockの生成と読出しは、各反復で1 byteずつ進める。最大2 blockの貸出制限により、遅いconsumerへのbackpressureが残る。
2 gate×2所有条件×4 seedの16実行。seedはyield位置の選択に使い、OSのthread実行順を固定するものではない。

回収順にはgate取得時に記録した単調ticketを付ける。pop／take直後のthread別ticketを保持し、join後にまとめて順序を照合する。
関数return後の記録・検証が他threadより遅れることがあるため、callback完了順をFIFO順の証拠にしない。
このticket・counter・vectorはhostの検査器だけに存在する。

## 検査と結果

ASan／UBSanはnative22件＋workspace規則3件の25件、別buildのThreadSanitizerはnative22件が通過した。
どちらも実行終了0、sanitizer診断出力なし。conftest.pyは追加していない。

| 条件 | 実行数／sanitizer | message回収数／sanitizer | block返却数／sanitizer |
|---|---:|---:|---:|
| fixed、2 gate×4 seed | 8 | 9,600 | 1,280 |
| shared、2 gate×4 seed | 8 | 19,200 | 2,560 |
| 別threadがgate保持 | 2 | ― | ― |
| worker停止位置での回復、2 gate×2所有条件 | 4 | ― | 新しい64 byteを各1回確認 |

stressだけで合計57,600 messageと7,680 blockを回収した。転送速度・MCU時間・成功確率ではない。

- messageのproducer・sequence・反転値・check値を検査。回収ticket順に各producerの全sequenceが1回ずつ現れることを確認した。
- blockのproducer・sequence・反転値と全64 byteのpatternを検査。取得ticket順に各producerの全sequenceが1回ずつ現れることを確認した。
- 全stressのjoin後に、未回収message・Ready・Writing・Readingがないことを検査した。observerは並列中に呼ばない。
- 別threadがgateを持つ間、push／pop／reserve／write／publish／take／read／release／ack／recover／attest／restartの12操作が即時拒否され、出力値が変わらないことを確認した。
- 回復試験は片方が17 byte書込み、もう片方が既存blockを17 byte読んだ位置で待つ。coordinatorが回復を開始し、workerは閉じた入口から操作できないことを確認して静止を通知する。
- producer／consumerの静止宣言だけではrestartを拒否する。DMA等のwriterに相当する宣言を追加するとepoch2へ進み、再利用後の旧呼出し5個を拒否し、新しい64 byteを正常回収した。

gate取得attempt数とBusy数は実行ごとに記録した。検査器自体のatomic counter、sanitizer、OS schedulingを含むため、方式の性能順位や公平性には使わない。
これらの有限実行とTSanの成功は、全interleavingでの証明ではない。

## この検査の境界

hostの標準C++同期を使ったときに、同じ所有台帳を2 threadから利用できたという証拠。
H417のSRAM配置・MMIO・IPC／HSEM・生成命令・cache・DMA可視性は確認していない。
HSEMの成功判定の記述差も[予備調査](h417-preparation.ja.md)から未解決のまま。

回復試験はworkerを明示した停止位置へ進め、旧API呼出しが戻ったことをhost atomicで確認する。
任意の命令での片側resetや、旧DMAが物理bufferへ書き続ける場合を再現しない。
DMA役の静止bitは試験側の宣言で、DMAを動かした証拠ではない。
主threadはcoordinator役として追加で存在し、物理2 core上の実行配置を再現したものでもない。

所有header・ABI入口・builder等、前回のABI入力hashはすべて一致した。
この変更はhost driverだけなので、前回の32 ELF・未使用12構成の0 B・構造費用を引き継ぎ、新しいMCU費用は測定していない。
hostのmutex／atomic backendをMCUへそのまま採用する決定ではない。

## 再現と次の作業

```sh
EXP05_H417_THREAD_EVIDENCE=work/h417-thread-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_threads.py tests/unit/test_workspace.py -q

EXP05_H417_THREAD_SANITIZER=thread \
EXP05_H417_THREAD_EVIDENCE=work/h417-thread-tsan-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_threads.py -q
```

TSanは別buildで実行し、ASanとは混ぜない。compilerとsanitizer runtimeが利用可能なhostを使う。
保存archiveは`work/exp05-h417-thread-20261021-evidence.tar.gz`。
source、実行binary、compiler／動的libraryのhash、compile command、44実行のstdout／stderr、JUnit、analysisを含む。
archiveのhash・byte数は測定JSONを参照する。

残るP4作業はmessage／blockの独立利用と依存除去、実行器への接続と共有実行器比較、
H417両imageの共有layout・起動handshake、実backend・DMA・reset。
教材では「取得順と完了順をどう区別するか」「Busy時のretryはどこにあるか」「host静止宣言を実DMA停止に置き換えるには何を観測するか」を追加で考える。
人による課題実施とレビューは未確認。
