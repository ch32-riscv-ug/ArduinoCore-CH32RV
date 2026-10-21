# EXP-04 保存領域のbyte予算付き走査と所有

2026-10-20。試作commit `bd87f48`。
[同期Storeの測定](store-result.ja.md)で残った全bank走査を、独立した読出し試作として分割した。
共有台帳で走査中と結果保持中の領域を所有し、解放後の古い結果を拒否する。
製品core・USB・既存の同期Storeは変更していない。key変更・書込み受理への統合と実Flashは未実装。
この境界を製品APIへ採用するかはP5で判断する。

## byte数で進行を制限する

`flash_scan.hpp`のLeasedScannerはbegin→明示poll→peek→endで扱う。
beginは所有を取得して状態を初期化するだけで、媒体を読まない。
pollは要求byte数と型で選んだ上限の小さい方まで読み、今回の上限は32とした。
要求0では読み進めず、過大な要求も32へ制限する。完了前のpeekは出力引数を変更しない。
peekとendも媒体を読まず、自動task・timer・callback・隠れたpumpは追加していない。

読出しは消去済み確認、header、payload、必要なら完了印を別の段階で進める。
CRCとschemaの検証は保持したbyteを使い、段階の切替え時に予算外の媒体readを足さない。
magicが消去済みに見えても後続が不明な内容なら、emptyと誤認しないよう領域末尾まで確認する。
RAWは既知長8 Bを読んでunvalidatedとし、世代やCRCがある扱いにはしない。

32という値は比較条件であり、時間保証ではない。
1回のpollが呼ぶbyte read APIの回数を制限したもので、Flashの物理transaction数やMCU cycleではない。
CRC・copy・台帳確認のCPU処理と、1回のreadのstallも別に評価する必要がある。

## 結果を使う間の所有

RegionTableは2枠の固定台帳で、canonicalな物理addressの半開区間を管理する。
領域の境界と長さを消去単位へ整列させ、範囲外、重複、同じownerの二重取得、満杯を拒否する。
今回の整列指定は2のべき乗に限定した。対象geometryの64／256／1,024 Bはこの範囲に入る。
隣接して重ならない区間は同時に所有できる。
台帳は宣言したdomainの境界を検査し、linker予約を自動で認識しない。製品接続では予約記述子と台帳の範囲を一致させる必要がある。

scannerは完了後もendまで所有を保持する。全ての協調する書込みが同じ台帳を使う条件なら、
peekは再走査せずに完了結果を返せる。共有台帳を経由しないRAW操作、別台帳、address aliasを自動的に防ぐ機能ではない。
Mへのread addressはgeometryの物理originから作り、link origin 0を物理addressとして登録しない。

取得ごとにticketを進め、ownerとticketが一致する場合だけ所有・解放を認める。
台帳のconfigureは所有中に拒否し、解放後のconfigureでもticketを巻き戻さない。
上限では再取得を拒否し、8 bit版で255取得と退役を検査した。
ticketの有効範囲は同じ台帳objectの寿命内であり、bootを跨ぐ識別子ではない。

end後は古い結果をpeekできない。再取得は新しい走査を開始する。
外側から所有を解放された場合も、peekは失敗し、次のpollはLostとなって媒体を読まない。
古いscannerのendが別のownerの新しい所有を解放しないことも検査した。
台帳はscannerより長く生存させ、破棄前にendする。copy／moveは禁止し、destructorでの暗黙解放はしない。

台帳は直列呼出しだけの試作で、ISR／別coreの同時取得やhardware barrierは含まない。
結果の保持が長ければ、他の利用者も長く待つ。公平な待機queue、予約、期限、強制取消はまだ提供しない。
操作中だけの所有と、mount期間の所有は、費用・無効化条件・待ち方を別に比較する。

## 検証

ASan／UBSan付きnative検査は次を確認し、Python形式模型の読出し結果へ照合した。

- 成立81条件×要求1／7／32／最大uint32の**324件**。各条件でemptyと正常2版保存後の結果を確認。
- 容量不足**24件**は所有も媒体readも行わない。
- CRC、key schema、同世代bank、版、header後の不明なbyte、完了印の不一致。
- 重複拒否、離れた領域の同時所有、範囲・整列、ticket、保持中のconfigure拒否、古い結果の無効化。

scanner／台帳 **353件**、既存Python形式 **200件**、workspace **3件**の**556件**が成功した。
conftestを追加せず、禁止ルールも維持した。
begin・peek・endのreadが0、各pollがmin(要求,32)以内、全read addressが所有領域内であることもcommand単位で検査した。
媒体の変更は台帳で保護した別ownerからのhost注入で、実program／eraseを実行した検査ではない。

V003 fast-erase-1k・4単位の空領域では、beginとpeekのreadは0だった。
pollへの最大uint32指定も32へ制限され、**128回で4,096 byte**を読み、emptyを確定した。
予算0では進行しないことと、完了後のpollで追加readしないことも記録した。
従来のStoreのbegin／putをこのscannerへ接続した結果ではなく、それらの同期走査はまだ残っている。

## 読出し専用のABI比較

V003／X035のLTO off／onで、同期読出し、所有付き予算読出し、台帳API単体、未使用を**76構成**へlinkした。
同期側は既存Storeのloadだけを呼び、書込みAPI・program bufferが必要ない読出し対照とした。
所有やpollを持たない同期側と、同じ能力として順位付けしない。
媒体境界は前回と同じLTOなしの仮実装で、Flash・startup・MCU実行を含まない。

| LTOあり | V003 image／静的RAM | X035 image／静的RAM |
|---|---:|---:|
| RAW同期読出し | 148／44 B | 164／44 B |
| RAW所有付き予算読出し | 876／132 B | 860／132 B |
| key 2 bank同期読出し | 828／44 B | 812／44 B |
| key 2 bank所有付き予算読出し | 1,624／172 B | 1,564／172 B |
| 台帳API単体 | 376／96 B | 392／96 B |

所有付きreader本体はRAW **36 B**、blob／key **76 B**、共有台帳は**44 B**だった。
tableの44 Bは利用者ごとの値ではない。今回の2枠台帳を共有する分を計上する。
入力・観測用の変数と仮backendも静的RAMに含め、stackとは分けた。
同期対照のStore objectは除去されており、0 Bの永続状態でloadの走査を一度に実行する条件である。

未使用のreader／台帳を宣言した**4構成は追加image／静的RAM 0 B**で、初期化配列と動的確保のsymbolもなかった。
保存予約は別列に残し、読出し方式を変えてもその容量を消費しないことにはしない。
全76構成でELFのload範囲、bin、全RAM section、保存予約のNOBITS属性を照合した。
`.su`は単独frameの情報であり、call-chainやIRQのstack peakではない。

## 証拠と次の比較

[全76行のCSV](scan-size-matrix.csv)と[入力・tool・JUnit・native binary・trace・archiveのJSON](scan-measurements.json)を保存した。
今回のnative binaryはfixtureの明示設定で検査直後に保存し、同じhashのbinaryで予算と所有のtraceを再確認した。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_scan.py tests/unit/core_redesign/test_flash_formats.py \
  tests/unit/test_workspace.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_flash_scan.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-scan-abi-recheck
```

native証拠を保存する場合はpytestへ`EXP04_SCAN_EVIDENCE=新しい出力先`を設定する。出力先は上書きしない。
保存したコピーを直接実行する場合は実行permissionが必要。byteのhashと検査結果はJSONで確認できる。

後続の[更新への統合](integrated-result.ja.md)でnative234件・新規ABI76を追加し、同じticketの保持中に受理／key変更の再走査を除いた。
既存の読出し76構成も再生成し、全image hash・静的RAMがこの測定と一致した。
走査結果を使う条件と追加状態は後続の結果を参照し、今回の読出し専用費用と区別する。
媒体操作のissue／poll／停止／buffer回収、長期所有と操作単位の所有、待機queue、追記・整理も残る。
実再program規則、物理中断範囲、CPU／IRQ停止、upload保持、MCU時間は別に検証する。
