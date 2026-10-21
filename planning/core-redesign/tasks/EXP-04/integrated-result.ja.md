# 所有付き走査から書込み受理・key変更への統合

2026-10-21。実装commit `6d46ea9`。
[予算付き走査](scan-result.ja.md)の結果を同じ領域所有のまま利用し、受理時とkey変更時の同期再走査を除く経路を追加した。
[全76 ELF](integrated-size-matrix.csv)、[host・hash・対照・失敗記録](integrated-measurements.json)を保存した。
製品コア・USBは変更していない。既存の実験用Storeにはprivateな準備入口を追加したが、public同期APIとdata memberは変えていない。
製品API・保存形式・所有期間の採用はP5で扱う。

## 同じ所有の間だけ走査結果を使う

新しいIntegratedStoreは既存のByteScannerとStore writer、共有RegionTableを接続する。
openでこのinstance自身をownerとして領域全体を取得し、明示pollで走査する。
Readyの間はcached snapshotからpeek／get／put／erase_key／begin_updateを処理し、媒体を再読しない。
書込み受理まで同じticketを保持することで、協調する別ownerが途中で内容を変更することを防ぐ。
snapshotをcallerから渡すpublic APIにはせず、所有と走査が確認できるadapterだけがwriterのprivate入口を呼ぶ。

| 操作 | 媒体アクセス | 結果・状態 |
|---|---|---|
| open | なし | 取得成功ならScanning |
| poll(read budget) | 最大min(budget,32)回のbyte read | 完了でReady。Writingでは書込みを進めない |
| peek／get | なし | Readyかつ同じ所有がある場合だけcached値を返す |
| begin_update／put／erase_key | なし | 入力をwriterへcopyし、成功でWriting。旧cached値を無効化 |
| write_step | 最大1回の同期erase／program | 完了でDone、媒体がfalseを返すとFailed |
| rescan | なし | Ready／Done／Failedから明示的にScanningへ戻る |
| end | なし | volatile計画とcached値を無効にし、所有を返却 |

read予算0ではreadしない。write_stepを別にしたので、poll(0)が消去を始めることもない。
erase／programの1回は物理時間の短さを意味せず、CPU・IRQ停止やstallの上限は未確認。
writerへ渡す相対offsetはadapterでgeometryのcanonicalなphysical originへ変換し、scannerも同じphysical領域を読む。

受理はvolatileな更新計画の受理で、永続化完了ではない。
Done後も古いsnapshotのpeek／getと次の更新を拒否し、再走査を要求する。
書込み成功を根拠に検証済みsnapshotを合成したり、裏で読出し確認したりはしない。
keyの変更も1回のowned scanの結果を使い、従来のchange→beginで起きていた二重走査を行わない。

この所有はopenからendまで保持する条件。ReadyやDoneのまま長く止まれば、別ownerも待つ。
操作単位で解放・再取得する方式や待機公平性を採用したわけではない。
同じ台帳を使う直列caller、固定geometry、媒体操作が戻った時点で静止していることが前提。
別台帳、address alias、台帳を迂回したRAW書込み、ISR／別coreの同時操作を防ぐ仕組みではない。

## 失敗と所有喪失

同期媒体がfalseを返した後はFailedで所有を保持し、そのままのwrite_stepは追加操作を発行しない。
FailedでもFlashの値が旧値のままとは限らず、選んだbitの全てが反映された失敗も模型に含む。
明示rescanで実内容を読み、Corruptなら明示的な置換許可、Ambiguousなら更新拒否という既存の規則を適用する。
同じMediumのprogram履歴を保持したまま再走査・復旧更新まで検査し、全0xffを見て再program可能とは推測しない。

外部からticketを取り消された場合はpeek／getを拒否し、次の更新・poll・write_stepはLostとなり、媒体に触れない。
古いendが新しいownerの所有を解放しないことも確認した。
endはserialな操作境界でvolatile計画を止めるだけで、途中の消去・programを巻き戻さない。
物理peripheralが操作後も動く非同期媒体や、timeout後の停止確認にはこの契約を流用できない。

## Hostの証拠

ASan／UBSanと`-Wall -Wextra -Werror`で統合native234件＋workspace3件、計237件が通過した。
全234 trace・46,611 command、byte read API 232,869回、同期媒体操作3,089回を記録した。
各write_stepのoperation・offset・長さ・data・成否と、最終媒体の全byte、再走査のsnapshotを既存の独立Python形式／媒体模型と照合した。

- 確認済みgeometryとRAW／blob／key・1／2 bank・完了印の成立81条件。通常3版の更新とkey変更・削除を検査。
- 容量不足24条件は所有も媒体アクセスも行わない。
- V003 fast・4消去単位の7形式で、全29操作位置×3中断mask、計87条件。失敗後の停止・再走査・明示復旧まで進める。
- Scanning／Ready／Writing／Done／Failedの5段階でticket取消。置換ownerを残し、旧操作によるアクセスと解放を拒否。
- 読出し予算1／7／最大uint32の21条件、予算0、32 byteへのclamp、完了後の追加readなし。
- Corrupt／世代最大の12条件、同世代2 bankの4条件。回復flagでもAmbiguousを上書きせず、世代を周回しない。

受理・put・erase_key・get・peek・open・end・rescanの媒体readは0だった。
全readは所有する領域内、媒体への全read／writeで所有を確認した。
重複claimを拒否し、隣接領域の別ownerは同時に利用できることも確認した。
fixtureはtest module内に置き、conftest.pyは追加していない。

既存同期Storeの272件も成功した。
ただしこの272件は、初回の混在実行から切り出した成功部分である。
その実行全体は新harnessの紛らわしいfor字下げが警告errorとなり、統合側197 setup errorを含んで失敗した。
修正・境界検査追加後の統合側237件を最終結果とし、失敗log・XMLも保持した。
全体が成功した509件の単一runとしては扱わない。

## ABI費用と既存経路への影響

同期Store全体と、所有・予算走査・準備済み更新を持つ統合側を同じ形式・geometryで比較した。
volatile入力のprobe操作から読出し・書込み・key変更を使い、read-onlyの前回測定とは費用を区別する。
V003／X035、LTO off／onで新規76 ELFを測定した。

| LTOあり | V003 同期image／RAM | V003 統合image／RAM | X035 同期image／RAM | X035 統合image／RAM |
|---|---:|---:|---:|---:|
| RAW fast・1単位 | 796／156 B | 1,680／236 B | 800／348 B | 1,656／428 B |
| key 2 bank fast・2単位 | 2,504／172 B | 3,388／292 B | 2,432／364 B | 3,204／484 B |
| key 2 bank standard・2単位 | 2,476／108 B | 3,324／228 B | — | — |

全RAMには観測harness・opaque backend・alignmentを含み、保存予約は別列で保持する。
同期側は領域所有をcallerの前提にして一度に走査し、統合側は台帳と進行状態を持つので、同じ能力の順位比較ではない。
今回の静的RAM増分はRAWで80 B、blob／keyで120 B、共有2枠台帳は44 B。
統合object本体はV003 fastでRAW120 B／key172 B、standard key108 B、X035 fastでRAW312 B／key364 B。
scannerとwriterの状態を別々に持つため、共用可能な作業領域を詰めた下限値ではない。

既存のread-only測定も76構成を再生成し、前回の全image hashと静的RAMに一致した。
public同期処理に新しい準備入口が混ざった扱いにはせず、未使用のprivate入口は既存経路に費用を足していない。
新規・対照ともallocatorとconstructor arrayを検査した。新規76行の.dataは0 B。
未使用の統合object・台帳を宣言した4構成は追加RAM0 B、空4 B probeとimage hash一致。
この未使用対照の保存予約設定は0単位であり、利用者が別途予約した領域まで自動で返却する証拠ではない。
単一frameはCSVへ記録し、call-chain・IRQを含むstack peakとはしない。

最初の新規ABI試行は3 ELF後、harnessの入力生成に使った除算のruntime helperが未linkで失敗した。
測定対象外の入力生成をbit選択へ直した再試行が76 ELF通過。失敗のcommand・source snapshotも保存した。

## 再現と残るもの

```sh
env EXP04_INTEGRATED_EVIDENCE=work/exp04-integrated-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_integrated.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_flash_integrated.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-integrated-abi-NEW
```

出力先は新しいdirectoryを指定する。
local archiveは `work/exp04-integrated-20261021-evidence.tar.gz`、6,429,312 byte、
SHA-256 `be07ffd203e5b9dd7b002157b24c11aed8bac7094f8b04f3c1a472435bba3cbc`。
最終234 trace・native binary・入力snapshot、混在runの元XMLと272件の切出し、新規76＋対照76 ELF、
既存76対照image、map・symbols・disassembly・frame、compiler／tool hash、初回失敗、集計scriptを保存した。
linkは既存の予約検査を使うが、起動・実Flash・実電断・upload保持を検証したものではない。

予約記述子とruntime domainの一元化、操作単位の所有との比較、非同期媒体のissue／poll／停止とbuffer寿命、
追記・GC・wear、実再program規則・CPU／IRQ停止、用途との共存と実機時間は残る。
初期化時に全容量を先に読む方式・長期所有・32 byte上限の採否も、この試作だけでは決めない。
[教材案](integrated-exercise.ja.md)は、人による評価前。
