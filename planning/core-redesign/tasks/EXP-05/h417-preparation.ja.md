# H417の共有境界：一次資料と公開順序の予備検査

2026-10-21。P4のCORE-01に向けた準備。製品コア・USB・Flash実装を変更せず、SDKも実験内へ組み込んでいない。
[入力](../../h417-inputs.json)で5 PDFとSDK21ファイルのhash、commitを固定した。
[測定記録](h417-measurements.json)には検査入力・実行command・反例・保存archiveのhashを含む。
実行方式と公開APIの採用はP5へ渡す。

## 確認した能力と条件

資料は[RM英語V1.8](https://www.wch-ic.com/download/file?id=435)、
[DS英語V1.9](https://www.wch-ic.com/download/file?id=423)、
[QingKe V3 V1.5](https://www.wch-ic.com/download/file?id=368)、
[QingKe V5 V1.0](https://www.wch-ic.com/download/file?id=425)、
[RM中国語V1.8](https://file.wch.cn/download/file?id=532)。以下の頁は英語版の印刷頁。
英語4 PDFは手元の資料台帳ともhashが一致した。中国語版は今回の取得内容を固定した。

| 項目 | 根拠と実験上の扱い |
|---|---|
| コア | DS 1.4.1、RM 4.1：CORE0はV3F、CORE1はV5F。RMのMHARTID表も0／1。V5Fはout-of-order。対称な2 CPUと仮定しない |
| 命令 | DSと両core manualは両者のatomic拡張を記載。小型V003のISAや一般的なV3という名称から利用可能命令を推定しない。H417用toolchain・生成命令の確認は未実施 |
| SRAM | DS 1.4.2：共有code/data 512 KiB、V5F ITCM 128 KiB、DTCM 256 KiB。V3FからTCMへもアクセスする説明がある。TCMを無条件に他コア不可のprivate RAMと扱わない |
| 順序 | RM 4.1.1、p49–51：SRAMはNormal、周辺はDevice。store Normal→store Deviceも並べ替え可能な表があり、fenceによる順序制御を説明。共有データの書込み→公開→IPC通知を単一操作とみなさない |
| cache | DS／RMはV5Fの32 KiB instruction cacheを記載。命令の可視性とpayloadの可視性を区別する。今回の根拠からD-cache整合方式を確定していない |
| IPC | RM 4.4、p78–79：4 channel×8 status bit、32 bit message registerが4本。statusの通知・clear・enable操作とFIFOの所有・容量・回収は別。通知だけを未処理仕事の唯一の記録にしない条件を検査する |
| HSEM | RM 4.5、p83–87：32 channel、read-lock／write-read、CID／PIDによる解放と一括解放。取得できることと、payloadの公開順序が成立することは別 |

SDKは[openwch/ch32h417 commit 6d1e469](https://github.com/openwch/ch32h417/tree/6d1e469a5e9874a67613721ad7fd47b9de96654f)に固定。
取得fileのSHA256・Git blob SHA1と、commitのroot treeにおける所属を照合した。
これはGit署名の検証ではなく、固定した取得内容とmanifestの整合検査である。

## SDK例をそのまま契約にしない箇所

- `HSEM_DataSharing`の両linker scriptは同じ`RAM_SHARED`、`0x20178000`から32 KiBを予約し、`.shared (NOLOAD)`へ配置する。V5F側はV3Fの`shared.o`をINPUTする。片側の通常BSS初期化で共有状態も初期化されると仮定しない。この32 KiBは例の設定であり、コアの必須容量ではない。
- 同例はvolatile変数を使い、lock内でDelayMsも呼ぶ。有限callback・待たずに戻る規則の実装例としては採用しない。volatileだけで他コアとの公開順序を保証する根拠にもしていない。
- SDKのAMO helperには`.w`とcompilerのmemory clobberがあるが、共有SRAMからMMIOへの公開順序を一式保証するbackendと確認したものではない。SDKのinterrupt操作にある`fence.i`も、payload公開barrierとして流用しない。
- `NVIC_WakeUp_V5F`はaddressの下位10 bitを除いてwake設定する入口。wake操作を、旧実行・DMA・旧世代の書込みが停止した証明にしない。

HSEM成功判定には未解決の記述差がある。RM英語4.5.1はread-lockの返却値0を成功と説明し、中国語版の同節も同じ説明。
一方、固定SDKの`HSEM_FastTake`は返却値が`(currentCoreID << 8) | (1 << 31)`なら成功とする。
LOCK／CIDの更新説明だけでは、read時に更新前／後のどちらが返るのかを解決できなかった。
どちらを正とするかは未決定。実機では両coreで初回取得・競合・自己再取得・解放後の返却値をrawで保存する。

## 有限な公開順序模型

1 slot・1回の公開について、payloadの2 word書込み、publish、notify、check、2 word読出しの7 eventを列挙する。
consumerは通知を受けて1回checkし、publishを観測した場合だけ読出し値を受理する。
eventは外から見えた書込み／sampleした読出しの抽象表現。C++ thread、H417命令列、cache、投機の修復を再現するものではない。

独立した3条件を切り替える。

- release条件：両payload書込みがpublishより先に見える。
- acquire条件：checkより後に両payloadをsampleする。
- 通知順序条件：publishがnotifyより先に見える。

これらは模型への制約であり、実装済みのfenceやmemory orderではない。

| release | acquire | 通知順序 | 列挙順序数 | 受理した不完全payload | 1回checkで未公開 |
|---|---|---|---:|---:|---:|
| 無 | 無 | 無 | 2,520 | 1,260 | 840 |
| 無 | 無 | 有 | 840 | 630 | 0 |
| 無 | 有 | 無 | 420 | 72 | 252 |
| 無 | 有 | 有 | 84 | 36 | 0 |
| 有 | 無 | 無 | 840 | 174 | 504 |
| 有 | 無 | 有 | 84 | 36 | 0 |
| 有 | 有 | 無 | 140 | 0 | 124 |
| 有 | 有 | 有 | 4 | 0 | 0 |

行同士には同じ順序も含まれ、発生確率やCPU時間ではない。
安全なpayload公開だけを与えても通知が先行すると、唯一のcheckが早すぎる反例を持つ。
未処理状態が保持され、後で明示pollすればこの模型では回収できるが、その遅延上限は与えない。
通知を再送するのか、定期pollするのか、通知順序を保証するのかは次の比較条件。

新規検査18件（公開順序10、資料監査8）とworkspace規則3件、計21件が通過した。
監査は内容・長さ・Git blob・commit・tree・truncated tree・所属fileの破損を拒否する検査を含む。
conftest.pyは追加していない。ABI・Flash／RAM・実CPU時間は今回測定していない。

## 次に作る比較と実機への入口

1. 既定用途の16 byte message×4枠と64 byte block×2個について、単一コア、コア別owner、共有実行器を同じ入力で比較する。所有・回収・満杯・通知集約・古いticketを記録し、共有方式を採用済みにしない。
2. 両imageの共有領域・通常RAM・stackをlink結果で照合する。起動前の共有初期化担当、開始handshake、世代の確定時点、再起動時のNOLOAD状態を明示する。
3. 公開／取得／再利用の境界をbackendへ分け、生成命令・配置・alignmentを確認する。HSEM／AMO／IPCとSRAM／MMIOの組合せで必要な順序を実機確認する。汎用fence設定を先に決めない。
4. 片側停止・再起動と全chip resetを分け、core・DMA・周辺のどれが動き続けるかを調べる。timeoutや世代更新だけでは旧writerの物理書込みを止められない。再利用には静止の証拠が必要であり、HSEM強制解放だけでblockを返却しない。
5. その後、V003最小imageへH417用storage・table・初期化が逆流しないことをlinkで測る。今回のPython模型では追加0 Bの実証をしていない。

教材では「通知が届いたらデータも完成しているか」「通知bitを消せばbufferを再利用できるか」「相手のtimeoutで所有を回収してよいか」を先に予測し、保存した反例と比較する。
人による課題実施・理解度のレビューは未実施。
