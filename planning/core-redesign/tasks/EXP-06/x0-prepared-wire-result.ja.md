# N02 X0用途とX035 clock・route・pin準備の接続

2026-10-21。実装commit `f01ef67`。入力は [x0-prepared-wire-inputs.json](../../x0-prepared-wire-inputs.json)。
X0のR/A/B/C-static-batch・直接／通知方式へ、既存X035 Setup／route adapterを接続した。
Master・Manager・Managed・Domain・直接／通知adapter・rendererと用途の意味処理は変更していない。
製品コア・USB／Networkも編集していない。実行方式や公開APIの採用ではない。

## 共通部分と置き換えた境界

新しい `X0PreparedWire` は既存直接／通知portから派生し、停止・poll・通知・take・cancelを共用する。
開始時のadmissionだけ、boolean pinsではなく `Setup::ready()` をManagerのlease内で呼ぶ。
開始時に自動prepareせず、利用者側の明示prepareを前提にする。
低水準のvalidation・copy・submitの規則は既存portと同じ。
実験の差を小さくするため、元のboolean pins fieldは残して未使用にした。最終APIの最適な配置とは扱わない。

準備は公称HSI／HPRE、gate、周辺idle、route／pin、debug解放宣言を照合する。
一度preparedになっていても、後から値が変わればopen時に拒否し、自分のleaseだけを返す。
ready確認には32 bitと16 bitのMMIO読出しが必要なため、拒否がMMIO全体0件であることは要求しない。
拒否した取引の書込み・受理・storage loanが発生しないことを検査した。
これは公称値とregister模型の一致であり、clock精度や電気的なpin状態ではない。

prepare途中の故障はSetup自身のleaseを保持する。呼出し元がquiescentを宣言してabandonするまで、
新しいdriverのopenを許さない。abandonは周辺reset／停止を代行しない。
debug routeは既存の解放宣言が必要で、adapterは勝手にdebuggerを無効化しない。

## 用途としての結果

- C8のroute 0〜7、F7のroute 2〜7で、R/A/B/Cと直接／通知を比較した。
  正常入力の用途状態・表示全byteとhashは独立whole-image／transaction模型に一致した。
- 同じboard模型を事前設定したboolean対照と、正常時の用途状態・受理・loan・wire byteが一致した。
  wire刺激中のMasterのMMIOも一致。open／prepareには追加の読出し・設定があるため、その部分を同じ費用とはしない。
- clock／gate／route／pinをprepare後に壊し、受理0、loan未開始、lease返却を確認。
  値を戻して再prepareした後は、confirmed=5020、計測5000 mV／1000 mAと最終表示へ回復した。
- gate／route／pinの書込みを無視する故障ではFaultHeldとなり、二度目のprepareはBusy。
  abandon(false)では返さず、明示abandon(true)後にだけ回復できた。
- F7のroute 0／1はUnsupportedPackageでprepare・通信を開始しない。MMIOも発生しない。
  拒否例を正常に動く用途のサイズ順位へ混ぜない。
- debug解放宣言を省いたrouteではFaultHeldとなり、勝手なdebug無効化書込みはなかった。

旧結果を保持した例では、転送中の再prepareをResourceBusyで拒否した。
停止・reap後、利用者take前にDomainを48→12 MHzへpublishし、RCC模型が古いままならClockMismatch。
HPRE模型を合わせて再prepareした後、peerが周辺を取得し通信開始した。
旧結果のhint配達とtakeは、そのpeerのMMIO・leaseを変更しない。
peer終了後の新取引は、C2=12、400 kHz指定のtiming=0x800aを使った。
これらはregister値の検査で、実際の48／12 MHzや400 kHzを測ったものではない。

準備flagは通信中のreadyを意味せず、外部ownerをすべて検出するregistryでもない。
USBやdebugとの実pin競合、強制clock変更中の物理挙動は未検証。
PPSは引き続き仮想PDで、実source相手に期限を守った結果には読み替えない。

## 検査・記録

最終native182件＋workspace3件、計185件成功。ASan／UBSan付き16実行物を使用した。
既存plugin由来warning370件。remote CI成功ではない。
初期runはreadinessの読出しを許す条件へ検査を直すため、53件通過の時点で明示中断した。
その旧source・実行物・JUnitも残し、最終件数へ重複加算していない。

840 trace、入力2,476,232操作、16 bit MMIO／critical 12,164,184 eventとboard 32 bit 676,048 eventを保存。
package・route・mode・入力ごとの210群すべてでR/A/B/Cの全観測行が一致した。
各MMIO／critical時に、その操作のownerがDomainを保持することをassertした。
明示ready／pclkだけはread-only probeとして区別する。

正常用途は独立oracleとboolean対照へ照合し、故障・再準備・peerはliteralと全群一致で追加検査した。
この区別を保ち、全故障のwhole-image oracleや全interleavingの証明とはしない。
今回の全route／packageを対象にしたseed付き乱数は追加していない。
前回の乱数成功を新しい準備条件の検証済み範囲へ移さない。

## ABI費用と容量の範囲

[全546構成](x0-prepared-wire-size-matrix.csv)はC8のstructural memory map、LTO有無で、
empty、boolean対照の4群×直接／通知×使用／未使用、
全16 package／route組合せの4群×直接／通知×使用／未使用をbuildした。
constructor array・allocatorは検出されなかった。
準備objectのDomain参照は初期値imageへ含み、暗黙のruntime constructorを常設しない。

F7ラベルもこのC8用Flash／RAM mapでpackage guardコードを比較したもので、F7実機の容量適合ではない。
unsupportedな使用行32件は拒否経路のimageであり、動作する完成用途として順位付けしない。
この実験からV003への対応や費用も結論しない。

C8 route 0・LTOあり、単位B。括弧は同じ入口のboolean対照。

| 群 | 直接Flash | 直接静的RAM | 通知Flash | 通知静的RAM |
|---|---:|---:|---:|---:|
| R | 6920（6312） | 732（716） | 7968（7376） | 804（788） |
| A | 6928（6328） | 732（716） | 7984（7384） | 804（788） |
| B | 7376（6816） | 764（748） | 8440（7904） | 836（820） |
| C-static-batch | 7464（6904） | 748（732） | 8464（7896） | 820（804） |

準備による静的RAM差は16 B。元のboolean fieldを残したwrapper全体の差で、15 Bの単純置換とはしない。
全群に68 Bのharnessを含み、Flashには.data初期値も含む。
今回の入口にはprepare／abandon操作があるため、前回のimageと同じとは仮定せず、今回のpair同士を比較する。
新しいMMIO確認を持つ方式とboolean宣言だけの方式は、故障検出機能が異なる。

未使用272行は追加image・静的RAM・用途objectが0 B、empty binaryと一致した。
これはこのC8 structural matrixの結果で、実行中にremoveすればRAMが返る意味ではない。
最大単一frameは表の元CSVに記録し、stack peakとは区別した。
startup、実clock／pin、IRQ、実PD、stack peak、保存予約込みのB1は未検証。

## 再現とN02の到達点

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_prepared_wire.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_x0_prepared_wire.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-prepared-wire-abi>
```

`EXP06_X0_PREPARED_EVIDENCE` は未作成directoryを指定し、入力・生成fake・16実行物・gzip traceを保存する。
今回も専用TMPDIRを使用。conftestは追加していない。
[測定記録](x0-prepared-wire-measurements.json)にtool、hash、JUnit、全trace、ABIと移送archiveを記録した。
移送archiveは `work/exp06-x0-prepared-20261021-evidence.tar.gz`、91,454,840 B、
SHA-256 `92972128888f3a0ded5b969bc1cda10880d5d6078c112726c01703539875635a`。

N02の通常モデルで予定したhost／ABIの用途接続は、直接回収、通知満杯・解除／retry、
RX2末尾、X035準備と旧結果保持まで実施した。
実機、実read命令間のIRQ、controller初期化と変換品質、公開callback破棄のIO-04、利用者評価は別の未検証。
N02のhost／ABI範囲の実施を、P4全体や全契約の完成にはしない。
次の通常作業はN03のK0共有境界。[教材案](x0-prepared-wire-exercise.ja.md)も人のレビュー前。
