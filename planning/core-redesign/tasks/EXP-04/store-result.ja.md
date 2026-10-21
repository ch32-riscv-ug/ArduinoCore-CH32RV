# EXP-04 保存形式のC++試作とABI費用

2026-10-20。試作commit `9c4e76d`。
[Python形式模型](format-result.ja.md)と同じRAW／blob／数値key、上書き／2 bank／別単位の完了印をC++へ移した。
初回保存・既存値の更新中断を照合し、V003／X035のABIで容量と未使用除去を測った。
製品core・USB・boards.txtはこの作業では変更していない。実Flash driver・起動・MCU実行は含まない。
公開APIや保存方式の採用はP5で判断する。

## 実装の範囲と媒体境界

`flash_store.hpp`は保存形式と保護条件を型引数で選び、geometryを別policyへ分離した。
設定値8 B、または最大2件の数値key payload 12 Bを受理時にコピーする。
program用bufferは1単位分で、targetでは2／64／256 Bへ特殊化する。
保存領域全体やbank全体のRAM cacheは持たない。
CRC・世代はformatted側に持ち、RAWではその管理領域を持たない。
永続形式はC++ structのmemory表現を書かず、固定位置のlittle-endian byte列を生成する。

begin→明示step→完了の間に1媒体操作ずつ進める。受理時の入力、key変更の一時配列を、その後参照し続けない。
keyのput／get／削除、世代上限、同世代bankの曖昧さ、破損領域の明示置換も比較へ含めた。
copy／moveを禁止し、abandonは進行状態を閉じるだけでrollbackしない。

媒体境界はbyte readと、同期したerase／programを要求する。
erase／programは失敗時も操作が停止済みで、呼出し後に作業bufferを参照し続けないことが前提。
falseを返すtimeout後もhardwareが動いている実装を、この境界へ接続してはならない。
そのようなbackendには、未確定・停止・回収を扱う別の寿命契約が必要になる。
RAWとformatted処理の独占・直列操作は呼出し側の前提で、leaseや共有lockの実装はまだない。

native検査は最大256 Bの作業bufferとruntime geometryを使う。同じtemplateのtarget側ではgeometryを固定する。
host検査の成功は、target命令を実行した証拠ではない。

## Python模型との照合

ASan／UBSan付きnative driverへcommand列を与え、独立に実装したPython形式模型と照合した。
各commandの結果、active状態、phase、読出し値・世代・bank、媒体全体のhash、erase／programの順序・address・全書込みbyteを比較する。
driver側の観測用走査は、媒体操作の進行に数えない。

- 正常・容量不足：7候補×5 geometry×予約1／2／4の105条件。
- 初回保存の中断：成立する81条件で3,721条件。
- 2版を保存した後の置換中断：同じ81条件で3,721条件。
- 世代上限、同世代の曖昧さ、明示置換、key schema／上限：5検査。

合計native **272件**、従来のPython形式 **200件**、workspace **3件**の**475件**が成功した。
conftestを追加せず、禁止ルールも維持した。
中断は操作境界とbyte mask `00／01／07／55／aa／ff`、前半byteだけの変化を使い、計**7,442条件**を再更新まで確認した。
初回は旧値がないため、途中のempty／corruptも区別する。
置換の2 bank候補では、今回の集合で有効な旧値または新値となった。
上書きでの旧値消失、明示置換での再初期化を、旧値の復旧と混同しない。

新しいscannerは同じhost媒体の試行履歴を引き継ぐ。
実reset、CRC衝突、隣接領域への影響、全bit組合せ、電源・耐久性条件の証明ではない。

## ABIの費用

7候補のうち容量が成立する81条件をLTO off／onで162構成へlinkした。
さらにempty 4、未使用・予約なし28、未使用・明示予約あり4を含め、計**198 ELF**を生成した。
容量不足24条件はnativeで副作用なしの拒否を確認し、使用可能なABI構成として数えていない。

媒体部分はFlashへ接続せず、別translation unitの仮実装をLTOなしでlinkする。
read addressとprogram byteを観測するため、engine側から定数として消せない境界になっている。
測定には仮実装と全APIを呼ぶ入口、必要なmemcpy／memsetも含めた。
実Flash driver・startup・clock・割り込みを含むCore全体や完成アプリの容量ではない。

| LTOあり、fast mode | V003 image／Store本体／静的RAM | X035 image／Store本体／静的RAM |
|---|---:|---:|
| RAW、予約1単位 | 752／82／156 B | 752／274／348 B |
| blob上書き、予約1単位 | 1,576／92／164 B | 1,524／284／356 B |
| blob 2 bank、予約2単位 | 1,764／92／164 B | 1,716／284／356 B |
| key 2 bank、予約2単位 | 2,448／96／168 B | 2,368／288／360 B |
| key 2 bank＋完了印、予約4単位 | 2,544／96／168 B | 2,452／288／360 B |

Store本体には設定値と作業bufferを含む。V003 fastの作業bufferは64 B、X035 fastは256 B。
V003 standardの作業bufferは2 Bで、同じLTO条件のStore本体はRAW 20 B、blob 28 B、key 32 Bだった。
standardとfastは消去粒度・予約量・操作回数が違い、RAMの小さい方を同じ保証の勝者とはしない。

入口の変数60 Bと仮backendの変数12 Bも静的RAMに含む。RAWには配置paddingもある。
保存予約は別列で計上し、imageへpaddingとして足していない。
このprobeのimage＋予約量は各partのFlashに収まるが、stack等を含む完成アプリの収容は未検証。

未使用・予約なしの**28構成は追加image／静的RAM 0 B**で、Storeとbackendのsymbolも除去された。
未使用でも明示予約した4構成は、追加code／RAMは0 Bだが、保存予約はV003で256 B、X035で1,024 B残る。
型を宣言しただけの費用と、容量を明示して確保した費用を分ける。
動的確保のsymbolと初期化配列は見つからなかった。

全198構成でELFのfile-backed load範囲とbin、RAMに配置された全section、保存予約のNOBITS属性、image＋予約の境界を確認した。
`.su`の最大単独frameはV003 72 B、X035 112 Bで、call-chainやIRQを含むstack peakではない。
初回のkey長判定でV003に剰余の補助演算が必要になった診断も残した。
最終試作は、最大2件で許される長さ0／6／12を直接判定し、同じ意味を照合している。

## 短く分割できていない処理

後続で[byte予算付き読出しと共有所有](scan-result.ja.md)を独立に検査した。
以下のStoreのbegin／putはまだ同期方式のままで、後続scannerを統合した扱いにはしない。

今回のload／beginは全bankを同期走査する。
消去済みに見えるheaderだけでなく、後続に不明な内容がないか確認するための走査も含む。
key変更は現在値の走査後にbeginでも走査するため、重複する。
V003 fast-erase-1k・4単位予約の消去済み媒体では、beginで4,096回、putで8,192回のbyte readを観測した。
これは呼出し本体の模型上の回数で、観測用loadを除く。MCU cycleや時間ではない。

従って、作業RAMをbank全量から減らせたことと、呼出しを十分短く分割できたことは別である。
次は走査の進行状態・1回のbyte予算・更新との排他、cacheした状態の無効化条件、
媒体操作のissue／poll／回収を分ける方式を比較する。
物理FlashがCPUを止める場合は、APIを非同期に見せるだけで通信との共存を成立させない。

## 記録と再実行

[全198行のCSV](store-size-matrix.csv)、[入力・tool・JUnit・cut監査・archiveのJSON](store-measurements.json)を保存した。
archiveにはnative入力、ABIのELF／map／disasm／bin／symbol／stack情報、正常・診断command、追加artifact検査を含む。
元のpytest実行binaryはtmp清掃で残っていない。入力・flags・JUnitを保存し、読出し回数の分析binaryは別に再buildしたものと明記した。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_store.py tests/unit/core_redesign/test_flash_formats.py \
  tests/unit/test_workspace.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_flash_store.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-store-abi-recheck
```

ABI記録は未作成の出力先を指定する。実Flashやboardへuploadしない。
実媒体条件、RAW／keyの所有、buffer回収、追記・整理・wear、より大きな値やkey、schema移行、
保存中のCPU／IRQ・通信の共存、実upload保持は引き続き残る。
形式の採用や既定容量はこの測定では決めない。
後続の[走査・所有と更新の統合](integrated-result.ja.md)では、同じ所有ticketを保った準備済み更新を別経路で比較した。
この同期経路のpublic APIは維持し、private準備入口を追加した際の既存272件と読出し76 imageの再検査を記録している。
