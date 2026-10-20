# V006休止：資料と待機境界の予備検査

2026-10-20。P4のSLEEP-01を準備する資料照合と有限模型。方式の採用はP5へ渡す。
コア本体・USB・option byteを変更せず、実機の休止・復帰・電流は未測定。
[入力](../../sleep-inputs.json)と[source／数値記録](v006-sleep-measurements.json)で版・hash・再現手順を固定した。

## 一次資料で確認した境界

[WCH RM V1.5](https://www.wch-ic.com/download/file?id=399)、
[DS V2.0](https://www.wch-ic.com/download/file?id=396)、
[QingKe V2 manual V1.3](https://www.wch-ic.com/download/file?id=369)を公式downloadから取得し、手元の資料台帳とPDF hashが一致した。
本文の頁は印刷頁を先に記し、括弧内にPDF先頭からの頁を示す。

| 項目 | 調査結果と次の確認 |
|---|---|
| Sleep | RM 2.3、p16–17（PDF18–19）／DS 1.4.6、p7（PDF9）：CPU側の停止と周辺clockを区別する。省電力のために周辺を止めた場合の動作は別条件 |
| Standby | 同箇所：SRAM・register・pin状態を保持して実行継続する説明。高周波clockを停止し、復帰時はHSI。系列共通の「deep sleep＝reset」にはしない |
| 復帰 | EXTI・AWU等による復帰と、NRST／IWDGによるresetを区別する。保持される説明だけで実I/Oを使用可能とみなさず、clock・転送停止・再設定を照合する |
| 動作し続けるもの | RM 2.3.3はLSIとIWDGを挙げる。SysTickからStandbyの経過時間を得られる根拠は得ていない。Flash操作完了までStandbyへ入らないという条件もある |
| wake設定 | RM 2.3.4：AWUはLSIを使いEXTI line9と接続。SDK例はevent・fallingの設定。IRQ通知が必ず実行されると推定せず、eventとinterruptを区別する |
| 待機命令 | core manual p12–13／18（PDF13–14／19）：WFIと、WFITOWFEを使うWFEの説明。SEVONPENDは新しいpendingの遷移を扱うため、既存pendingを再通知する前提にしない |
| debug | core manual 5章はdebug中の休止制限を記載。RMのdebug controlにもモード別の記載がある。probe接続中の観測を静止電流・通常復帰の証拠にしない |

資料の記述差も未解決として残す。DS 1.4.6は「3 modes」と書きながらSleep／Standbyの2個を列挙する。
第3モードを存在するものとして補わない。
RM 3.1 p21–22（PDF23–24）は`STANDBY_RST`の説明と、18.5のUSER bit2の表でreset enableの極性が一致しない。
USER表は1でreset無効、0で有効としている。章の説明だけを根拠にoption byteを変更しない。
[中国語版V1.5](https://file.wch.cn/download/file?id=472)の3.1、p8（PDF19）とUSER表p212（PDF223）にも同じ記述差があり、英訳だけの問題として解決できなかった。
同版FLASH_OBRのSTANDY_RSTはactive lowと記す、p207（PDF218）。この一致を記録しつつ、実機のoption byte・復帰結果は未確認として残す。
またRM debug controlのStandby復帰のresetに関する説明を、通常の保持復帰へ無条件に適用しない。

## 固定したSDKの入口

[openwch SDK commit 189c91b](https://github.com/openwch/ch32v002_004_005_006_007/tree/189c91b555bfb3d22e9c1f41db9b6707e7fc19ab)の6ファイルを保存し、
取得したroot treeとGit blob hash・SHA256で照合した。

- [core_riscv.h](https://github.com/openwch/ch32v002_004_005_006_007/blob/189c91b555bfb3d22e9c1f41db9b6707e7fc19ab/EVT/EXAM/SRC/Core/core_riscv.h#L301)：`_WFE`と`__WFE`を区別する。後者はeventの準備と2回のwfiを含み、`__WFI`も後者を呼ぶ。
- 同headerのdeep条件ではIRQ無効化とEXTI event設定の一時変更、復帰後のIRQ有効化がある。元のIRQ許可状態を保存・復元する汎用guardと同じ契約とは扱わない。
- [PWR入口](https://github.com/openwch/ch32v002_004_005_006_007/blob/189c91b555bfb3d22e9c1f41db9b6707e7fc19ab/EVT/EXAM/SRC/Peripheral/src/ch32v00X_pwr.c#L163)はPDDSとSLEEPDEEPを設定し、helperから戻るとSLEEPDEEPをclearする。元の全設定の復元やアプリのidle確認は代行しない。
- [Standby例](https://github.com/openwch/ch32v002_004_005_006_007/blob/189c91b555bfb3d22e9c1f41db9b6707e7fc19ab/EVT/EXAM/PWR/Standby_Mode/User/main.c)はLSI ready待ち、AWU設定、復帰後の出力初期化を持つ。sampleの同期ready待ちを有限stepのコア契約として採用しない。

名前がWFIだから単一命令のatomicな入口だと扱うことも、後述の抽象反例をそのままSDKの不具合だと扱うこともできない。
実adapterでは命令列、最終確認の位置、event残存、IRQ状態、wake sourceを合わせて検証する必要がある。

## 休止直前の通知を304通りで比較

Python模型はstickyな「仕事あり」とhardware pending、wake event、IRQ mask、眠っている状態を別々に持つ。
通常文脈はIRQ有効で開始する。通常文脈の操作間に0／1／2回の通知を入れ、末尾の待機後も刺激する。
eventを保持するbackendでは、古いeventの有無も変える。通知は条件を集約するbitであり、全入力edge数を保持するqueueではない。
**MCU emulator、SDK helperの命令実行、memory orderingの検証ではない。**

| 手順 | IRQがmask中もwakeする仮定 | IRQがmask中はwakeしない仮定 | eventが次の待機まで残る仮定 |
|---|---:|---:|---:|
| 確認→待機 | 2／10 | 2／10 | 0／20 |
| 確認→event消費→待機 | 5／15 | 5／15 | 4／30 |
| event消費→確認→待機 | 2／15 | 2／15 | 0／30 |
| mask→確認→maskのまま待機→復帰後unmask | 0／15 | 9／15 | 0／30 |
| mask→確認→unmask→待機 | 9／21 | 9／21 | 0／42 |

各セルは「仕事またはhardware pendingが残って眠り続ける反例数／刺激数」。条件付き0はこの有限集合の結果であり実機保証ではない。
同じ手順でもbackend条件が変われば反例になるため、単に再確認回数を増やすだけでは契約が閉じない。

代表反例は「idle確認→ISRが仕事を記録して戻る→既にhardware pendingは消えた状態で待機」。
event方式でも「idle確認→新通知がeventを立てる→古いeventを消すつもりで消費→待機」は反例になる。
maskを使う案は待機後にIRQ状態を戻す境界まで含む。その方式で起床できるかを資料と実機で確認せず採用しない。
通知がないときに眠ること、仕事を見て中止したときにmaskを戻すことも検査した。

新規54件とworkspace規則3件、合計57件が通過した。conftest.pyは追加しない。
[全15条件](v006-sleep-boundary-matrix.csv)と、artifact内の全304 traceを残した。

## AWUは設定値と実経過を分ける

DS Table 3-12、p40（PDF42）のLSIはmin／typ／maxが90／128／172 kHz。
RM 2.4.4–5はwindow＋1とprescalerを説明する。SDKの15設定を候補とし、window 0..63の960通りと、
SDKの「0x3F未満」というcommentに合わせた0..62の945通りを比較した。範囲差を黙って決定しない。
同じ時間の別encodingは区別し、5目標×3選び方×2上限の30条件を全候補と照合した。

10秒を目標とし、上限window63の理想的なカウンタ時間だけを計算した結果は次のとおり。

| 計算上の選び方 | divider／window値 | 172 kHz時 s | 128 kHz時 s | 90 kHz時 s |
|---|---:|---:|---:|---:|
| 名目値を最も近くする | 61,440／20 | 7.5014 | 10.08 | 14.3360 |
| 最速clockでもカウント時間10秒以上 | 61,440／27 | 10.0019 | 13.44 | 19.1147 |
| 最遅clockでもカウント時間10秒以下 | 61,440／13 | 5.0009 | 6.72 | 9.5573 |

これは周波数を表の範囲と仮定した整数カウンタの計算。起動・設定位相・復帰時間・IRQ／通常処理の遅延は含めず、外部入力は途中でもwakeし得る。
「10秒以内にuser処理が実行される」「10秒経過した」という保証には使えない。
SDK例のdivider10,240／window25も名目2.08秒であり、外部wakeがあればその時間すら経過していない。
windowやclockの名目値を[論理時刻](../EXP-01/logical-time-result.ja.md)へ既知elapsedとして足さない。
独立した経過の根拠がない復帰では、unknown停止としてdomainを切り替える接続を今後検査する。

## 残る通常作業と実機事項

- 通常10秒→表示中の休止要求→cancel→停止確認→buffer回収→wake設定→復帰→再初期化を、既存I/Oと論理時刻の模型に接続する。
- 参加者を用途側で明示列挙する案と任意manager案を、同じ資源・通知・入力条件で比較する。全機能登録を必須にはしていない。
- 休止準備中の新規入力、wake設定変更、保持できない完了結果、登録解除、失敗時rollbackを比較する。
- 実V006の正確なpart・option byte・clock／pin・debug条件を固定し、最終idle確認の前後、眠りに入った後の刺激を観測する。
- 実命令列とpending／eventの可視性、元のIRQ状態、resetか継続か、復帰後のclock・I2C・display・WDTを確認する。
- 電流はMCUとboard全体を分け、表示電源・未使用pin・probe接続・LSI／IWDG条件を記録する。

今回の模型は判定手順の必要条件を絞る材料であり、V006用途全体やSLEEP-01の実機成立を完了したものではない。
ABIサイズ・CPU時間・禁止時間・静止電流は未測定。未測定の欄を0 Bや0 µAで埋めない。
[教材](v006-sleep-exercise.ja.md)は利用者の変更・予測結果の評価前である。
