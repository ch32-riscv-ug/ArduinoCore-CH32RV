# 利用者レビュー03：OEP probe用途からの評価

2026-10-22。利用者がこのセッションへ提示したレビューを、提案者が論点別に整理した記録。
**逐語原文ではない。以下の評価・数値はレビュー者の報告であり、提案者による再測定ではない。**
レビュー者によるこの整理の再確認は未実施。応答と判断は[別文書](review-02-03-response.ja.md)へ分ける。

## 対象と独立性

- 対象：提案版2のusage、proposal、review-request。まずusageだけを読んだ第一印象を報告。
- レビュー者の申告：作成に関与していない、OEP開発のセッション。
- 用途：CH32X035／V307／H417向けprobe。RVSWD／SWIO bit-bang debug、target console、IRQ受信のfixture UART、Flash設定保存、serial素通し。
- 推奨：**Aの直接step。自動進行はsystemだけ（S限定）。Bはリンクから外せる任意部品なら許容。**
- R1との関係：独立した開発セッションからの用途・読みやすさの評価。回答付き資料を読んでおり、初学者の盲検予測や全反例の回答試験ではない。

## 指摘の整理

| ID | レビュー者の主張・要望 |
|---|---|
| U1 基本形 | Aはloopの呼び順を読めば分かる。K0のBは概念とRAMを増やすだけで、基本教材へ併記する利点がない |
| U2 接続負担 | X0のBでもstep内の条件確認は残り、setupのbindが増える。条件変更時にstepとbindの両方を直す必要がある |
| U3 接続漏れ | Aの余分な確認より、Bのbind漏れによる停止は再現・発見が難しく、初学者に厳しい |
| U4 容量 | X035 OEPの使用量は62,568／63,488 B、残り920 Bとの報告。模型のB増分+268〜+488 B image、+24〜+28 B静的RAMを踏まえると、この用途ではBを選べない。未使用Bの除去を必須にしたい |
| U5 自動進行 | delayの場所による挙動とuserの途中介入が分かりにくい。自動はsystemだけ、userは明示runに限定し、AutoEligible等とruntime.attach(app)を不要にしたい |
| U6 登録手順 | attach、runtime参加、発生元bind、初期postの4段階は多い。attachで初回ready、期限bindを一操作にまとめたい |
| U7 通知と回収 | bindCompletionToOwnerとbindAvailableの違いが見えにくい。複数宛先への起動通知と、単一ownerの回収権を命名でも区別したい |
| U8 手動service | 既定はbeginで自動登録。ManualServiceは容量を削る上級者向け構成と位置づければよい |
| U9 単独の期限部品 | Aにもexecutor不要のDeadline／Periodを示し、expired確認・次回期限設定・再試行を共通部品で書けるようにしたい |
| U10 Bの評価用途 | 休止の要る用途で比較してほしい。readyなしが分かることと共通の計測口がBの強み。USB給電probeでは休止の利点は出ない |
| U11 接続漏れ診断 | 全workを毎巡走らせる任意診断を提案。通常時と動きが変わればbind漏れを疑える |

## 支持された境界

begin成功によるsystem自動登録、tryStart／tryTake／Busyの区別、回収までbufferを貸し出す規則、
容量付きRing、ISR共有の同期区別は用途に合うとの評価。
現行probeは実質Aで、USB処理→console→UART受信→素通しを順に進めると報告された。
数msのdebug bit-bang、IRQ禁止区間、timeoutまで待つ操作中はsystemが進まないが、USB側はIRQ／hardwareで受けているとの報告。
レビュー者はこれを非再入・時間制約をIRQ／hardwareへ分担する方針を支持する実例とした。

## 証拠としての限界

OEPのソース版、ELF、map、linker予約、最適化条件、実通信条件はこのレビューに添付されていない。
容量値や成立条件を新しい再現済みbenchmarkとして登録しない。既存K0／X0模型とOEPの差分実験でもない。
レビュー者の用途適合と好みを記録し、初学者一般の教育効果・全系列のUSB耐性・Bの最終費用を証明した扱いにはしない。
