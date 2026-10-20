# H417向け所有契約：message、block、回復

2026-10-21。[一次資料と公開順序](h417-preparation.ja.md)に続く、CORE-01の直列host／小型ABI実験。
[入力](../../h417-ownership-inputs.json)、[数値・hash](h417-ownership-measurements.json)、[全32構成](h417-ownership-size-matrix.csv)を固定した。
製品コア・USB・Flash実装は変更していない。実行方式・公開APIの採用はP5へ渡す。

## 比較する所有規則

| 条件 | message投入／block生成 | message回収／block取得 | blockの借用中 |
|---|---|---|---|
| single | core0 | core0 | 取得したcoreが返却 |
| fixed | core0 | core1 | 取得したcoreが返却 |
| shared-access | core0／1 | core0／1 | 取得したcoreだけが操作・返却 |

shared-accessは両coreへの操作権限を与える条件。共有実行器・callback配送・実threadの並列実行を実装したものではない。
固定ownerとの違いを、同じ所有台帳で比較する準備である。
各操作はGateを1回だけ試し、Busyなら待たずに失敗する。hostでは直列のfake Gateを使い、競合は明示的に注入する。
実backendの排他・memory ordering・cache・DMAは提供していない。

messageは16 byte×4枠のFIFO。満杯では拒否し、回収できた値だけを返す。
blockは64 byte×2個。Free→Writing→Ready→Reading→Freeと進む。
今回の生成元はbyteを順番に書くfake producerで、64 byte全部が揃うまでpublishできない。
Readyの順序は予約順でなくpublish順。consumerの回収と返却は別操作で、返却順は自由。
raw pointerは公開せず、CPU／DMAがそのpointerへ書き続ける動作を安全にした証拠にはしない。

tokenはdomain・epoch・generation・indexを持つ。借用中は生成／取得したcoreも照合し、古いtoken・別domain・別epoch・二重publish／返却を拒否する。
generationとepochは非周回。8 bit感度条件では枯渇すると拒否し、restartでもgenerationを巻き戻さない。
domainはstorageの生存期間に対し一意であるという外部条件。古い利用者が残る状態で同じdomainのFabricを作り直すことは対象外。
実reset後のNOLOAD領域、起動ID、両imageの初期化handshakeは別途必要。

message／blockの通知はそれぞれbitで集約する。ackしてもFIFO・Ready・Readingを解放しない。
逆に回収しても通知bitは自動clearしない。通知と所有が独立であることを検査するための条件であり、最終APIの決定ではない。
pollで台帳を読めば通知を失っても仕事が残るが、pollされる時間上限は置かない。

## 回復でできることとできないこと

core0だけがbegin_recoveryを実行できる。開始後はmessage／block操作を閉じ、データと既存借用をそのまま保持する。
producer・consumer・DMA等のwriterの3役について、adapterが静止を宣言し、全部揃ってからrestartできる。
restartでは未回収message／Ready／借用を破棄してFreeへ戻し、epochを更新する。データ保持を保証する回復ではない。
payloadをzero化する動作も行わず、次の正しい完全publish以外から読めないことを確認する。

この宣言はhardware停止の証拠ではない。timeout・通知ack・HSEM強制解放・epoch変更だけで宣言してよい規則にはしていない。
旧coreやDMAが本当に動き続ける場合、checked APIが古いtokenを拒否しても物理書込みは止まらない。
実reset範囲・DMA停止・静止確認・反対coreの合意を結ぶadapterは未実装。

## Hostの証拠

ASan／UBSan、`-Wall -Wextra -Werror`を使い、native79件とworkspace規則3件、計82件が通過した。
fixtureはtest module内に置き、conftest.pyは追加していない。
別表現のPython模型はlist／所有台帳を使い、C++のring indexを共有しない。
79 traceの全76,697操作で、成功／失敗、出力保持、message列、全block状態、既知prefixのhashを照合した。

- message満杯・FIFO・wrap・通知ack後も残る仕事。
- 2 blockのpublish逆順・返却逆順・両方借用中の満杯・古いtokenによる再利用中領域への操作拒否。
- 8通りの静止maskと3所有条件。ReadingとWritingが同時に残る状態で、静止が不足すればrestartと全通常操作を拒否。
- 空／部分／全量Writing、Ready、Readingの5状態で段階的な静止宣言。未回収messageの破棄、古いepochの拒否、新しいpayloadへの置換。
- fake GateのBusy、誤owner・domain・epoch・index・書込み順序・範囲・不完全publish、回復管理の権限。
- generation単独、epoch単独、両方の8 bit枯渇。再起動後もticketを再利用しない。
- 4 seed×600生成選択×3条件。75選択ごとに回復と完全なmessage／block交換を入れ、失敗ばかりのtraceにならないようにする。

追加の共通158操作を保存したnative binaryで再生した。
block ownerのcore番号だけをsingleのcore0へ正規化すると、3条件の全snapshotが一致した。
message受理4・拒否1、FIFO 0／1／2／3、block取得はgeneration2→1、逆順返却、静止不足の拒否、新epochと古い書込みの拒否を確認した。
これは同じ進行機会を与えた直列比較であり、コア間の公平性・parallel性能・時間保証の比較ではない。

## 小型profileでの構造費用

GCC 14.3.0、`-Os`、V003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、LTO off／onで32 ELF。
empty・raw payload192・3所有条件・未使用3条件をそれぞれbuildした。
H417用ISA／linker／startupを使ったfirmwareではなく、小型profileへ所有構造を持ち込んだときの比較である。

| 項目 | 両profile・両LTOのbyte数 |
|---|---:|
| payload本体 | 192 |
| Fabric（payloadと管理情報） | 236 |
| fake Gate | 2 |
| 操作・引数・出力等の測定harness | 112 |
| `.data + .bss`合計（alignment込み） | 356 |

Fabricのpayload以外は44 B。Gateは別所有のreferenceで、実HSEM／atomic backendの状態量や費用を含めない。
raw payload対照のharnessは76 Bであり、image／RAM差分をすべて管理機構だけの費用とみなさない。
Fabricはmessageとblockを合わせて持つ試作。messageだけ／blockだけのstorage除去や共有epochの扱いは、まだ比較していない。

| profile | LTO | single image B | fixed image B | shared-access image B |
|---|---|---:|---:|---:|
| V003 | off | 1,904 | 1,928 | 1,944 |
| V003 | on | 1,816 | 1,832 | 1,864 |
| X035 | off | 1,864 | 1,888 | 1,904 |
| X035 | on | 1,808 | 1,824 | 1,848 |

emptyは8 B。未使用12構成は追加RAM 0 B、Fabric／Gateのsymbolなし、image hashが各emptyと一致した。
全32構成でallocator symbol・constructor arrayを要求せずlinkした。
発行された最大単一stack frameは使用条件でV003 76 B、X035 128 B。call chain・割込み込みのstack peakではない。
image差はこの入口・操作集合・fake Gateの結果であり、H417や実同期backendの優劣を示さない。

## 再現と残る作業

```sh
EXP05_H417_OWNERSHIP_EVIDENCE=work/h417-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_ownership.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_h417_ownership.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/h417-abi-new
```

保存archiveは`work/exp05-h417-ownership-20261021-evidence.tar.gz`。
source snapshot、compilerとbinaryのhash、79 full trace、ELF／map／disasm／stack資料、共通入力の再生とanalysisを含む。
同名出力先は再利用せず、hashとbyte数は測定JSONを参照する。

残るP4作業は、message／blockを独立利用した場合の依存除去、実行器への接続、固定ownerと共有実行器の比較、
両imageの共有layoutと起動handshake、H417 backendと生成命令、実可視性・DMA・片側reset。
[hostの実thread同期](h417-thread-result.ja.md)は追加検査済み。所有headerとABI入力は変更していない。
[教材](h417-ownership-exercise.ja.md)は準備したが、人による実施は未確認。
